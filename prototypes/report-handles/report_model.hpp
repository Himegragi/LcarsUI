// THROWAWAY prototype: report handles for ADR 0003 Q135-Q143. Not framework code, not an API proposal.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rh {

enum class Kind : std::uint8_t { Result, CallbackEnd, WorkEnd, LeaseEnd, HandoffAck, ResultAck };
enum class SlotState : std::uint8_t { Free, Reserved, Filled, Dropped };
// Q141: an invalid report changes nothing and the sender gets the reason back.
enum class Reject : std::uint8_t { None, Empty /*moved-from or already used*/, WorldGone, Revoked };
enum class Contract : std::uint8_t { Handover, ExplicitAck };  // Q137 fixture contracts
struct Payload { int value = 0; bool cancelled = false; };

inline const char* name(Reject r) {
    switch (r) { case Reject::None: return "受理"; case Reject::Empty: return "把手已用過或為空"; case Reject::WorldGone: return "所屬環境已結束"; case Reject::Revoked: return "已撤權或已失效"; }
    return "?";
}

// Shared between owner and workers. All storage is allocated in the constructor;
// reserve/fill/drop/drain never allocate (Q135: retention must not depend on spare memory).
class Ledger {
public:
    struct Ref { std::uint32_t index = 0, gen = 0; };
    struct Drained { Ref ref; Kind kind; std::uint64_t obligation; SlotState st; Payload p; };
    struct RejectDetail { Reject reason; std::uint32_t index, gen; std::uint64_t seq; };
    Ledger(std::size_t cap, std::size_t detailCap) : slots_(cap), details_(detailCap) {}

    std::optional<Ref> reserve(Kind k, std::uint64_t ob) {
        std::lock_guard g(m_);
        for (std::uint32_t i = 0; i < slots_.size(); ++i) if (slots_[i].st == SlotState::Free) {
            auto& s = slots_[i]; s.st = SlotState::Reserved; s.kind = k; s.ob = ob; s.p = {}; ++live_; return Ref{i, s.gen};
        }
        return std::nullopt;
    }
    Reject fill(Ref r, Payload p) {
        std::lock_guard g(m_);
        auto& s = slots_[r.index];
        if (s.gen != r.gen || s.st != SlotState::Reserved) { note_locked(Reject::Revoked, r); return Reject::Revoked; }
        s.st = SlotState::Filled; s.p = p; return Reject::None;
    }
    void drop(Ref r) {  // handle destroyed unused
        std::lock_guard g(m_);
        auto& s = slots_[r.index];
        if (s.gen == r.gen && s.st == SlotState::Reserved) s.st = SlotState::Dropped;
    }
    void revoke(Ref r) noexcept {  // owner: invalidate an outstanding handle (reassign, abandon)
        std::lock_guard g(m_);
        auto& s = slots_[r.index];
        if (s.gen == r.gen && s.st != SlotState::Free) { s.st = SlotState::Free; ++s.gen; --live_; }
    }
    // Owner copies up to N filled/dropped slots without freeing them. Workers cannot change a
    // filled/dropped slot, so it stays put until the owner calls complete() after applying it.
    template <std::size_t N> std::size_t peek(std::array<Drained, N>& out) const {
        std::lock_guard g(m_);
        std::size_t n = 0;
        for (std::uint32_t i = 0; i < slots_.size() && n < N; ++i) {
            auto& s = slots_[i];
            if (s.st != SlotState::Filled && s.st != SlotState::Dropped) continue;
            out[n++] = {Ref{i, s.gen}, s.kind, s.ob, s.st, s.p};
        }
        return n;
    }
    void complete(Ref r) noexcept {  // each settled slot is returned exactly once here
        std::lock_guard g(m_);
        auto& s = slots_[r.index];
        if (s.gen == r.gen && (s.st == SlotState::Filled || s.st == SlotState::Dropped)) { s.st = SlotState::Free; ++s.gen; --live_; }
    }
    bool holds(Ref r, std::uint64_t ob) const {
        std::lock_guard g(m_);
        auto& s = slots_[r.index];
        return s.gen == r.gen && s.ob == ob && (s.st == SlotState::Reserved || s.st == SlotState::Filled || s.st == SlotState::Dropped);
    }
    std::size_t live() const { std::lock_guard g(m_); return live_; }
    std::size_t capacity() const { return slots_.size(); }
    void count_reject(Reject why, Ref r) { std::lock_guard g(m_); note_locked(why, r); }
    // Latest details, oldest first. Allocates (a query); recording never does.
    std::vector<RejectDetail> recent_rejects() const {
        std::lock_guard g(m_); std::vector<RejectDetail> v;
        const std::size_t n = std::min<std::size_t>(detailSeq_, details_.size());
        for (std::size_t i = 0; i < n; ++i) v.push_back(details_[(detailSeq_ - n + i) % details_.size()]);
        return v;
    }
    std::size_t rejected(Reject r) const { std::lock_guard g(m_); return rejected_[static_cast<int>(r)]; }
    // every non-free slot, for invariant checks
    std::vector<std::pair<Ref, std::uint64_t>> occupied() const {
        std::lock_guard g(m_); std::vector<std::pair<Ref, std::uint64_t>> v;
        for (std::uint32_t i = 0; i < slots_.size(); ++i) if (slots_[i].st != SlotState::Free) v.push_back({Ref{i, slots_[i].gen}, slots_[i].ob});
        return v;
    }
private:
    struct Slot { std::uint32_t gen = 1; SlotState st = SlotState::Free; Kind kind{}; std::uint64_t ob = 0; Payload p{}; };
    mutable std::mutex m_;
    std::vector<Slot> slots_;
    std::size_t live_ = 0;
    void note_locked(Reject why, Ref r) {
        ++rejected_[static_cast<int>(why)];
        if (!details_.empty()) details_[detailSeq_ % details_.size()] = {why, r.index, r.gen, detailSeq_};
        ++detailSeq_;
    }
    std::array<std::size_t, 4> rejected_{};  // exact per-category counters
    std::vector<RejectDetail> details_;     // ring of the latest N details, allocated once
    std::uint64_t detailSeq_ = 0;
};

class World;

// Q140/Q142: issued only by World when an obligation is taken; move-only; cannot be built from public ids.
class ReportHandle {
public:
    ReportHandle() = default;
    ReportHandle(const ReportHandle&) = delete;
    ReportHandle& operator=(const ReportHandle&) = delete;
    // A moved-from handle keeps a weak reference to its ledger (copying a weak_ptr only bumps a counter),
    // so a later report on the shell is still counted (Q147).
    ReportHandle(ReportHandle&& o) noexcept : l_(o.l_), r_(o.r_), live_(std::exchange(o.live_, false)) {}
    ReportHandle& operator=(ReportHandle&& o) noexcept {
        if (this != &o) { drop_if_live(); l_ = o.l_; r_ = o.r_; live_ = std::exchange(o.live_, false); }
        return *this;
    }
    ~ReportHandle() { drop_if_live(); }

    // Single use: after this call the handle is empty whatever the outcome.
    Reject report(Payload p = {}) {
        if (!live_) {  // used and moved-from handles still know their ledger; never-bound ones do not
            if (auto l = l_.lock()) l->count_reject(Reject::Empty, r_);
            return Reject::Empty;
        }
        live_ = false;
        auto l = l_.lock();
        if (!l) return Reject::WorldGone;
        return l->fill(r_, p);
    }
    bool valid() const noexcept { return live_; }
private:
    friend class World;
    ReportHandle(std::weak_ptr<Ledger> l, Ledger::Ref r) : l_(std::move(l)), r_(r), live_(true) {}
    void drop_if_live() noexcept { if (live_) { live_ = false; if (auto l = l_.lock()) l->drop(r_); } }
    std::weak_ptr<Ledger> l_;
    Ledger::Ref r_{};
    bool live_ = false;
};

struct DeliveryHandles { ReportHandle callback, work, lease, ack; };
// Q145: delivered on the owner thread; POD so sending it needs no allocation.
struct LostNotice { std::uint64_t handoff; Kind kind; };
// Q144: result of an owner declaration that a lost reporter's obligation has safely ended.
enum class Declare : std::uint8_t { Done, NotLost, Unknown };
// Pause/resume (ported from the JS model; ADR 0003 receive routes, shared-service isolation, recovery).
enum class EnvState : std::uint8_t { Running, Paused, Resuming, Faulted };
enum class Isolation : std::uint8_t { Undeclared, Unknown, Proven };
enum class ResumeEnd : std::uint8_t { Committed, Cancelled, Invalidated };
enum class Commit : std::uint8_t { Committed, Cancelled, BatchNotDone, Failed, TooLate, AlreadyEnded, Stale };

// Owner-thread state. Every mutating call checks the calling thread.
class World {
public:
    struct Command { int env; bool running = true; std::string terminal; int value = 0; Ledger::Ref ref; bool allowContinue = false, cancelRequested = false, cancelled = false; };
    // disp empty = UI disposition still pending (result held while its env is paused or cleanup is unsafe).
    struct Receipt { int env; std::string terminal; bool handedOver = false; std::optional<Ledger::Ref> ack; std::uint64_t ackOb = 0; std::string disp; };
    struct Outcome { std::uint64_t id; int env; std::string outcome; };
    struct Attempt { std::uint64_t id, route; std::vector<std::uint64_t> batch; std::size_t cursor = 0; };
    struct Env {
        std::string name; Contract contract; bool closed = false, ended = false, cleanupSafe = true; int value = 0;
        EnvState state = EnvState::Running; std::uint64_t route = 0; std::vector<std::uint64_t> services;
        Isolation isolation = Isolation::Undeclared; bool needsProof = false;
        std::optional<Attempt> attempt; std::deque<std::pair<std::uint64_t, ResumeEnd>> ended_attempts;  // bounded: 4
    };
    struct Route { std::uint64_t id; bool alive = true; };
    struct Service { std::uint64_t id; bool trustworthy = true; };
    struct Part { bool active = false, lost = false; Ledger::Ref ref; std::uint64_t ob = 0; };
    struct Handoff { int env; bool acked = false; std::uint64_t assignment = 0; Part cb, work, lease; std::optional<Ledger::Ref> ack; std::uint64_t ackOb = 0; };

    explicit World(std::size_t reportCap, std::size_t rejectDetails = 16) : ledger_(std::make_shared<Ledger>(reportCap, rejectDetails)) {}
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Q137: an env without a valid acceptance point is refused.
    // Without a route the env gets a dedicated one (keeps single-env scenarios short).
    std::optional<int> add_env(std::string n, std::optional<Contract> c, std::optional<std::uint64_t> route = std::nullopt,
                               std::vector<std::uint64_t> svcs = {}) {
        owner();
        if (!c || (*c != Contract::Handover && *c != Contract::ExplicitAck)) { ++refusedContracts; return std::nullopt; }
        if (!route) { route = add_route(); if (!route) return std::nullopt; }
        Env e{std::move(n), *c}; e.route = *route; e.services = std::move(svcs);
        envs.push_back(std::move(e)); return static_cast<int>(envs.size() - 1);
    }
    // Q135: reserve the report place before taking the obligation; no place, no obligation.
    std::optional<ReportHandle> start_command(int env, bool allowContinue = false) {
        owner();
        if (envs[env].closed || envs[env].state != EnvState::Running) { ++refusedNotRunning; return std::nullopt; }
        const auto id = next();
        auto r = ledger_->reserve(Kind::Result, id);
        if (!r) { ++refusedObligations; return std::nullopt; }
        try { commands[id] = Command{env, true, {}, 0, *r, allowContinue}; receipts[id] = Receipt{env, {}, false, std::nullopt, 0, {}}; }
        catch (...) { commands.erase(id); receipts.erase(id); ledger_->revoke(*r); throw; }
        return ReportHandle(ledger_, *r);
    }
    std::uint64_t fault(int env) { owner(); const auto id = next(); handoffs[id] = Handoff{env, false, 0, {}, {}, {}, std::nullopt, 0}; return id; }
    // Needs four places (callback/work/lease end + ack); all or nothing.
    std::optional<DeliveryHandles> deliver(std::uint64_t h) {
        owner();
        auto& x = handoffs.at(h);
        if (!lostReceiver_) { ++refusedNoReceiver; return std::nullopt; }
        if (x.acked || x.cb.active || x.work.active || x.ack) return std::nullopt;
        x.assignment = next();
        std::array<std::optional<Ledger::Ref>, 4> rs; std::array<std::uint64_t, 4> obs{};
        const Kind kinds[4] = {Kind::CallbackEnd, Kind::WorkEnd, Kind::LeaseEnd, Kind::HandoffAck};
        for (int i = 0; i < 4; ++i) {
            if (i == 2 && x.lease.active) continue;  // old lease still outstanding: reuse is not modelled
            obs[i] = next(); rs[i] = ledger_->reserve(kinds[i], obs[i]);
            if (!rs[i]) { for (auto& r : rs) if (r) ledger_->revoke(*r); ++refusedObligations; return std::nullopt; }
        }
        try { for (int i = 0; i < 4; ++i) if (rs[i]) partOwner[obs[i]] = h; }
        catch (...) { for (int i = 0; i < 4; ++i) if (rs[i]) { partOwner.erase(obs[i]); ledger_->revoke(*rs[i]); } throw; }
        DeliveryHandles d;
        x.cb = {true, false, *rs[0], obs[0]}; d.callback = ReportHandle(ledger_, *rs[0]);
        x.work = {true, false, *rs[1], obs[1]}; d.work = ReportHandle(ledger_, *rs[1]);
        if (rs[2]) { x.lease = {true, false, *rs[2], obs[2]}; d.lease = ReportHandle(ledger_, *rs[2]); }
        x.ack = rs[3]; x.ackOb = obs[3]; d.ack = ReportHandle(ledger_, *rs[3]);
        return d;
    }
    // Revokes the current ack right; old callback/work/lease obligations stay with their handles.
    bool reassign(std::uint64_t h) {
        owner(); auto& x = handoffs.at(h);
        if (x.acked) return false;
        if (x.ack) { ledger_->revoke(*x.ack); partOwner.erase(x.ackOb); x.ack.reset(); }
        return true;
    }
    // Owner settles at most N reports per call.
    std::size_t settle() {
        owner();
        std::array<Ledger::Drained, 8> buf; const auto n = ledger_->peek(buf);
        std::array<LostNotice, 8> pending; std::size_t np = 0;  // at most one notice per drained slot
        try {
            for (std::size_t i = 0; i < n; ++i) {
                if (auto lost = apply(buf[i])) pending[np++] = *lost;
                ledger_->complete(buf[i].ref);
            }
            finalize();
        } catch (...) { deliver_lost(pending, np); throw; }  // items already completed keep their notice
        deliver_lost(pending, np);  // the batch is finished: the receiver may change state
        return n;
    }
    void close(int env) {
        owner(); auto& e = envs[env];
        if (e.attempt) { end_attempt(e, ResumeEnd::Invalidated); e.state = EnvState::Paused; }
        request_cancel(env); e.closed = true;
        dispose_closed(env); finalize();
    }
    void set_cleanup_safe(int env, bool s) { owner(); envs[env].cleanupSafe = s; dispose_closed(env); finalize(); }
    // Receiver takes settled results; acceptance follows the env contract (Q137).
    std::vector<ReportHandle> collect(int env) {
        owner(); std::vector<ReportHandle> acks;
        if (envs[env].closed && !envs[env].cleanupSafe) return acks;  // F5: unsafe cleanup keeps results
        acks.reserve(receipts.size());  // push_back below cannot reallocate
        for (auto it = receipts.begin(); it != receipts.end();) {
            auto& [id, r] = *it;
            if (r.env != env || r.terminal.empty() || r.disp.empty() || r.handedOver) { ++it; continue; }  // held results wait
            if (envs[env].contract == Contract::Handover) { record(id, env, "已交付"); it = receipts.erase(it); continue; }  // record: strong
            const auto ob = next(); auto ref = ledger_->reserve(Kind::ResultAck, ob);
            if (!ref) { ++refusedObligations; ++it; continue; }  // stays undelivered; try later
            try { ackOwner[ob] = id; } catch (...) { ledger_->revoke(*ref); throw; }
            r.handedOver = true; r.ack = *ref; r.ackOb = ob; acks.push_back(ReportHandle(ledger_, *ref)); ++it;
        }
        finalize(); return acks;
    }
    // Q138: explicit owner action only; refused when cleanup is unsafe.
    std::optional<std::size_t> abandon(int env) {
        owner(); if (!envs[env].cleanupSafe) return std::nullopt;
        std::size_t n = 0;
        for (auto it = receipts.begin(); it != receipts.end();) {
            auto& [id, r] = *it;
            if (r.env != env || r.terminal.empty()) { ++it; continue; }
            record(id, env, "已放棄");
            if (r.ack) { ledger_->revoke(*r.ack); ackOwner.erase(r.ackOb); }
            it = receipts.erase(it); ++n;
        }
        finalize(); return n;
    }
    // ---------------- receive routes, shared services, pause/resume ----------------
    // Bounded route table: only dead, unreferenced routes are reclaimed; full table refuses.
    std::optional<std::uint64_t> add_route() {
        owner();
        if (routes.size() >= kRouteCap)
            for (auto it = routes.begin(); it != routes.end(); ++it)
                if (!it->alive && !route_referenced(it->id)) { routes.erase(it); break; }
        if (routes.size() >= kRouteCap) { ++refusedRegistry; return std::nullopt; }
        const auto id = next(); routes.push_back({id, true}); return id;
    }
    std::uint64_t add_service() { owner(); const auto id = next(); services.push_back({id, true}); return id; }
    // The route is gone: dependants stop taking ordinary work; a running recovery is invalidated.
    void lose_route(std::uint64_t r) {
        owner(); auto* x = route(r); if (!x || !x->alive) return;
        x->alive = false;
        for (std::size_t i = 0; i < envs.size(); ++i) {
            auto& e = envs[i]; if (e.route != r || e.closed) continue;
            if (e.state == EnvState::Running) pause(static_cast<int>(i));
            else if (e.state == EnvState::Resuming) { end_attempt(e, ResumeEnd::Invalidated); e.state = EnvState::Paused; }
        }
    }
    // Full fault path: env stops for good and a handoff opens. An untrusted shared service pauses every
    // other dependant and voids earlier isolation proofs.
    std::uint64_t fail_env(int env, std::optional<std::uint64_t> unsafeService = std::nullopt) {
        owner(); auto& e = envs[env];
        if (e.attempt) end_attempt(e, ResumeEnd::Invalidated);
        request_cancel(env); e.state = EnvState::Faulted;
        if (unsafeService) {
            service(*unsafeService)->trustworthy = false;
            for (std::size_t i = 0; i < envs.size(); ++i) {
                auto& o = envs[i];
                if (static_cast<int>(i) == env || o.closed || !depends_on(o, *unsafeService)) continue;
                o.needsProof = true; if (o.isolation == Isolation::Proven) o.isolation = Isolation::Undeclared;
                if (o.state == EnvState::Resuming) { end_attempt(o, ResumeEnd::Invalidated); o.state = EnvState::Paused; }
                if (o.state == EnvState::Running) pause(static_cast<int>(i));
            }
        }
        return fault(env);
    }
    void declare_isolation(int env, Isolation v) { owner(); envs[env].isolation = v; }
    // A new trusted resource is not by itself an isolation proof.
    std::uint64_t bind_new_service(int env, std::uint64_t from) {
        owner(); auto& e = envs[env]; const auto id = add_service();
        for (auto& s : e.services) if (s == from) s = id;
        if (e.needsProof && e.isolation == Isolation::Proven) e.isolation = Isolation::Undeclared;
        return id;
    }
    // Why resuming on `route` is not allowed now; nullptr when it is.
    const char* resume_blocker(int env, std::uint64_t r) const {
        const auto& e = envs[env];
        const auto* x = route(r); if (!x || !x->alive) return "接收途徑無效";
        if (e.closed) return "環境已關閉";
        if (e.state == EnvState::Faulted) return "故障實例";
        if (e.isolation == Isolation::Unknown) return "隔離未知";
        for (auto s : e.services) { const auto* v = service(s); if (!v || !v->trustworthy) return "依賴不可信的服務"; }
        if (e.needsProof && e.isolation != Isolation::Proven) return "共享故障後尚未證實隔離";
        return nullptr;
    }
    // Freezes the held results of this env as the batch; results settled later are not added.
    std::optional<std::uint64_t> begin_resume(int env, std::uint64_t r) {
        owner(); auto& e = envs[env];
        if (e.state != EnvState::Paused || resume_blocker(env, r)) { ++refusedResume; return std::nullopt; }
        Attempt a{next(), r, {}, 0};
        for (auto& [id, rc] : receipts) if (rc.env == env && !rc.terminal.empty() && rc.disp.empty()) a.batch.push_back(id);
        e.route = r; e.attempt = std::move(a); e.state = EnvState::Resuming;
        return e.attempt->id;
    }
    // Disposes up to `budget` batch results, re-checking conditions before each one.
    std::size_t resume_step(std::uint64_t attempt, std::size_t budget = 2) {
        owner(); auto* e = live_attempt(attempt); if (!e) { ++staleAttempts; return 0; }
        const int env = static_cast<int>(e - envs.data());
        std::size_t n = 0;
        while (n < budget && e->attempt->cursor < e->attempt->batch.size()) {
            if (resume_blocker(env, e->attempt->route)) { end_attempt(*e, ResumeEnd::Invalidated); e->state = EnvState::Paused; return n; }
            dispose_held(e->attempt->batch[e->attempt->cursor++], env); ++n;
        }
        return n;
    }
    // First legal settlement wins: commit and cancel race on the owner thread.
    Commit commit_resume(std::uint64_t attempt) {
        owner(); auto* e = live_attempt(attempt);
        if (!e) return ended_as(attempt) ? Commit::AlreadyEnded : Commit::Stale;
        const int env = static_cast<int>(e - envs.data());
        if (e->attempt->cursor < e->attempt->batch.size()) return Commit::BatchNotDone;
        if (resume_blocker(env, e->attempt->route)) { end_attempt(*e, ResumeEnd::Invalidated); e->state = EnvState::Paused; return Commit::Failed; }
        end_attempt(*e, ResumeEnd::Committed); e->state = EnvState::Running; ++commits;
        return Commit::Committed;
    }
    Commit cancel_resume(std::uint64_t attempt) {
        owner(); auto* e = live_attempt(attempt);
        if (!e) { auto h = ended_as(attempt); if (!h) return Commit::Stale; return *h == ResumeEnd::Committed ? Commit::TooLate : Commit::AlreadyEnded; }
        end_attempt(*e, ResumeEnd::Cancelled); e->state = EnvState::Paused; ++cancels;
        return Commit::Cancelled;
    }
    // Results that settled outside a frozen batch are disposed after reopening, in bounded rounds.
    std::size_t process_pending(int env, std::size_t budget = 2) {
        owner(); auto& e = envs[env];
        if (e.state != EnvState::Running || e.closed || resume_blocker(env, e.route)) { ++refusedResume; return 0; }
        std::size_t n = 0;
        for (auto& [id, rc] : receipts) {
            if (n >= budget) break;
            if (rc.env != env || rc.terminal.empty() || !rc.disp.empty()) continue;
            dispose_held(id, env); ++n;
        }
        return n;
    }

    // Q145/Q148/Q149: one receiver per World, required before callback/work/lease obligations are taken.
    // Q150: called on the owner thread after a settle() batch has finished.
    // Refused for an empty receiver (Q149 must not be bypassed) and while notices are being delivered
    // (replacing the receiver would destroy the function object that is running).
    bool set_lost_receiver(std::function<void(const LostNotice&)> r) {
        owner();
        if (!r || delivering_) { ++refusedReceiverChange; return false; }
        lostReceiver_ = std::move(r); return true;
    }
    // Q144: per-obligation owner declaration. The framework cannot verify it; it only records it apart
    // from reported ends. Callable from the lost receiver (Q150: notices arrive after the batch).
    Declare declare_ended(std::uint64_t h, Kind k) {
        owner();
        auto it = handoffs.find(h);
        if (it == handoffs.end()) return Declare::Unknown;
        Part* p = part(it->second, k);
        if (!p) return Declare::Unknown;
        if (!p->active || !p->lost) return Declare::NotLost;
        OwnerDeclaration d{h, k};
        make_room(declarations);  // allocate before any change
        p->active = p->lost = false; declarations.push_back(d);
        try_release(h);
        return Declare::Done;
    }
    // Q145: queryable current state.
    std::vector<LostNotice> lost_parts() const {
        std::vector<LostNotice> v;
        for (auto& [id, h] : handoffs) {
            if (h.cb.lost) v.push_back({id, Kind::CallbackEnd});
            if (h.work.lost) v.push_back({id, Kind::WorkEnd});
            if (h.lease.lost) v.push_back({id, Kind::LeaseEnd});
        }
        return v;
    }

    std::vector<std::string> invariants() const {
        std::vector<std::string> e;
        std::size_t expect = 0;
        for (auto& [id, c] : commands) if (c.running) { ++expect; if (!ledger_->holds(c.ref, id)) e.push_back("執行中命令沒有回報預留 " + std::to_string(id)); }
        for (auto& [id, h] : handoffs) {
            for (const Part* p : {&h.cb, &h.work, &h.lease}) if (p->active && !p->lost) { ++expect; if (!ledger_->holds(p->ref, p->ob)) e.push_back("交接義務沒有回報預留"); }
            if (h.ack) { ++expect; if (!ledger_->holds(*h.ack, h.ackOb)) e.push_back("交接確認沒有預留"); }
        }
        for (auto& [id, r] : receipts) if (r.ack) { ++expect; if (!ledger_->holds(*r.ack, r.ackOb)) e.push_back("結果確認沒有預留"); }
        if (ledger_->live() != expect) e.push_back("預留數 " + std::to_string(ledger_->live()) + " ≠ 存活義務 " + std::to_string(expect));
        for (std::size_t i = 0; i < envs.size(); ++i) if (envs[i].ended) {
            for (auto& [id, r] : receipts) if (r.env == static_cast<int>(i)) e.push_back(envs[i].name + " 已結束卻有未結清結果");
            for (auto& [id, c] : commands) if (c.env == static_cast<int>(i) && c.running) e.push_back(envs[i].name + " 已結束卻有執行中命令");
        }
        for (auto& v : envs) {
            if ((v.state == EnvState::Resuming) != v.attempt.has_value()) e.push_back(v.name + " 恢復狀態與嘗試不一致");
            if (v.closed && v.attempt) e.push_back(v.name + " 已關閉卻有恢復嘗試");
            if (v.ended_attempts.size() > 4) e.push_back("結局紀錄超過上限");
        }
        for (auto& [id, rc] : receipts) if (!rc.disp.empty() && rc.terminal.empty()) e.push_back("未終態卻已處置");
        if (routes.size() > kRouteCap) e.push_back("途徑表超過上限");
        std::map<std::uint64_t, int> seen; for (auto& o : outcomes) if (++seen[o.id] > 1) e.push_back("同一結果兩筆結清紀錄");
        for (auto& o : outcomes) if (receipts.count(o.id)) e.push_back("已結清卻仍有回條");
        return e;
    }
    const Ledger& ledger() const { return *ledger_; }

    std::vector<Env> envs;
    std::map<std::uint64_t, Command> commands;
    std::map<std::uint64_t, Receipt> receipts;
    std::map<std::uint64_t, Handoff> handoffs;
    std::vector<Outcome> outcomes;  // fixture: unbounded within one scenario
    struct OwnerDeclaration { std::uint64_t handoff; Kind kind; };
    std::vector<OwnerDeclaration> declarations;  // Q144: "擁有者宣告結束", kept apart from reported ends
    std::size_t lostNotified = 0, lostUnnotified = 0, receiverFailures = 0, reportedEnds = 0, refusedNoReceiver = 0, refusedReceiverChange = 0;
    std::size_t refusedContracts = 0, refusedObligations = 0, providerLost = 0;
    std::size_t refusedNotRunning = 0, refusedResume = 0, refusedRegistry = 0, staleAttempts = 0, commits = 0, cancels = 0, cancelRequests = 0;
    std::vector<Route> routes; std::vector<Service> services;
    static constexpr std::size_t kRouteCap = 6;
    std::atomic<std::size_t> ownerThreadViolations{0};  // the only field written by non-owner threads

private:
    // Every World call runs on the owner thread.
    void owner() {
        if (std::this_thread::get_id() != ownerThread_) { ++ownerThreadViolations; throw std::logic_error("非擁有者執行緒"); }
    }
    std::uint64_t next() { return ++seq_; }
    // Grow geometrically before the push so push_back cannot reallocate (keeps record() strong).
    template <class V> static void make_room(V& v) { if (v.size() == v.capacity()) v.reserve(v.empty() ? 8 : v.size() * 2); }
    void record(std::uint64_t id, int env, const char* o) {
        Outcome x{id, env, o}; make_room(outcomes); outcomes.push_back(std::move(x));
    }
    std::optional<LostNotice> apply(const Ledger::Drained& d) {
        std::optional<LostNotice> notice;
        if (d.kind == Kind::Result) {
            auto& c = commands.at(d.obligation);
            auto& r = receipts.at(d.obligation);
            const bool lost = d.st == SlotState::Dropped;
            std::string t1 = lost ? "提供者失效" : d.p.cancelled ? "已取消" : "已完成", t2 = t1;
            const auto& e = envs[c.env];
            // Running: apply now. Paused/resuming/faulted, or closed with unsafe cleanup: hold (disp stays empty).
            const bool hold = e.closed ? !e.cleanupSafe : e.state != EnvState::Running;
            std::string disp = hold ? std::string{} : (lost || d.p.cancelled) ? "無結果" : e.closed ? "不適用" : "已套用";
            c.running = false; c.terminal.swap(t1); r.terminal.swap(t2); r.disp.swap(disp);
            c.cancelled = lost || d.p.cancelled; c.value = d.p.value;
            if (lost) ++providerLost;  // Q143 a
            if (!hold && !lost && !d.p.cancelled && !e.closed) envs[c.env].value = d.p.value;  // F4
            return notice;
        }
        if (d.kind == Kind::ResultAck) {
            const auto id = ackOwner.at(d.obligation);
            auto it = receipts.find(id);
            if (it != receipts.end()) {
                if (d.st == SlotState::Filled) { record(id, it->second.env, "已交付"); receipts.erase(it); }
                else { it->second.ack.reset(); it->second.handedOver = false; }  // ack handle dropped: hand over again later
            }
            ackOwner.erase(d.obligation);
            return notice;
        }
        const auto h = partOwner.at(d.obligation);
        auto& x = handoffs.at(h);
        if (d.kind == Kind::HandoffAck) {
            x.ack.reset();
            if (d.st == SlotState::Filled) x.acked = true;
        } else {
            Part& p = d.kind == Kind::CallbackEnd ? x.cb : d.kind == Kind::WorkEnd ? x.work : x.lease;
            if (d.st == SlotState::Filled) { p.active = false; ++reportedEnds; }
            else if (!p.lost) { p.lost = true; notice = LostNotice{h, d.kind}; }  // Q143 b: not assumed ended
        }
        partOwner.erase(d.obligation);
        try_release(h);
        return notice;
    }
    Route* route(std::uint64_t id) { for (auto& r : routes) if (r.id == id) return &r; return nullptr; }
    const Route* route(std::uint64_t id) const { for (auto& r : routes) if (r.id == id) return &r; return nullptr; }
    Service* service(std::uint64_t id) { for (auto& s : services) if (s.id == id) return &s; return nullptr; }
    const Service* service(std::uint64_t id) const { for (auto& s : services) if (s.id == id) return &s; return nullptr; }
    bool route_referenced(std::uint64_t id) const {
        for (auto& e : envs) if (e.route == id || (e.attempt && e.attempt->route == id)) return true;
        return false;
    }
    static bool depends_on(const Env& e, std::uint64_t s) { for (auto x : e.services) if (x == s) return true; return false; }
    // Stop taking ordinary work; started commands without allowContinue get a cancellation REQUEST (not an end).
    void pause(int env) { request_cancel(env); envs[env].state = EnvState::Paused; }
    void request_cancel(int env) {
        for (auto& [id, c] : commands)
            if (c.env == env && c.running && !c.allowContinue && !c.cancelRequested) { c.cancelRequested = true; ++cancelRequests; }
    }
    Env* live_attempt(std::uint64_t id) { for (auto& e : envs) if (e.attempt && e.attempt->id == id) return &e; return nullptr; }
    std::optional<ResumeEnd> ended_as(std::uint64_t id) const {
        for (auto& e : envs) for (auto& [a, how] : e.ended_attempts) if (a == id) return how;
        return std::nullopt;
    }
    void end_attempt(Env& e, ResumeEnd how) {
        e.ended_attempts.emplace_back(e.attempt->id, how);
        if (e.ended_attempts.size() > 4) e.ended_attempts.pop_front();
        e.attempt.reset();
    }
    // Owner-side disposition of a held result (strings built first, then non-throwing swaps).
    void dispose_held(std::uint64_t id, int env) {
        auto it = receipts.find(id);
        if (it == receipts.end() || !it->second.disp.empty() || it->second.terminal.empty()) return;  // abandoned or already disposed
        const auto& c = commands.at(id);
        std::string disp = c.cancelled ? "無結果" : envs[env].closed ? "不適用" : "已套用";
        it->second.disp.swap(disp);
        if (!c.cancelled && !envs[env].closed) envs[env].value = c.value;
    }
    void dispose_closed(int env) {  // a closed env with safe cleanup turns held results into "不適用"
        if (!envs[env].closed || !envs[env].cleanupSafe) return;
        for (auto& [id, rc] : receipts) if (rc.env == env && !rc.terminal.empty() && rc.disp.empty()) dispose_held(id, env);
    }
    static Part* part(Handoff& x, Kind k) {
        return k == Kind::CallbackEnd ? &x.cb : k == Kind::WorkEnd ? &x.work : k == Kind::LeaseEnd ? &x.lease : nullptr;
    }
    // Fixture failure policy (ADR: extension-callback failure guarantees decide): a throwing receiver is
    // counted, its notice is not re-sent, and the remaining notices are still delivered.
    void deliver_lost(const std::array<LostNotice, 8>& pending, std::size_t np) noexcept {
        const bool outer = !delivering_; delivering_ = true;  // nested settle() from the receiver keeps the flag
        for (std::size_t i = 0; i < np; ++i) {
            if (!lostReceiver_) { ++lostUnnotified; continue; }  // cannot happen once Q149 holds; counted if it does
            try { lostReceiver_(pending[i]); ++lostNotified; } catch (...) { ++receiverFailures; }
        }
        if (outer) delivering_ = false;
    }
    void try_release(std::uint64_t h) {
        auto& x = handoffs.at(h);
        if (x.acked && !x.cb.active && !x.work.active && !x.lease.active) handoffs.erase(h);
    }
    void finalize() {  // Q136: a closed env ends only after its results are settled
        for (std::size_t i = 0; i < envs.size(); ++i) {
            auto& e = envs[i]; if (!e.closed || e.ended || !e.cleanupSafe) continue;
            bool busy = false;
            for (auto& [id, c] : commands) if (c.env == static_cast<int>(i) && c.running) busy = true;
            for (auto& [id, r] : receipts) if (r.env == static_cast<int>(i)) busy = true;
            if (!busy) e.ended = true;
        }
    }
    std::shared_ptr<Ledger> ledger_;
    std::thread::id ownerThread_ = std::this_thread::get_id();
    std::uint64_t seq_ = 0;
    std::function<void(const LostNotice&)> lostReceiver_;
    bool delivering_ = false;
    std::map<std::uint64_t, std::uint64_t> partOwner, ackOwner;
};

}  // namespace rh
