// THROWAWAY prototype: report handles for ADR 0003 Q135-Q143. Not framework code, not an API proposal.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
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
    switch (r) { case Reject::None: return "受理"; case Reject::Empty: return "空把手"; case Reject::WorldGone: return "世界已銷毀"; case Reject::Revoked: return "已撤權/已失效"; }
    return "?";
}

// Shared between owner and workers. All storage is allocated in the constructor;
// reserve/fill/drop/drain never allocate (Q135: retention must not depend on spare memory).
class Ledger {
public:
    struct Ref { std::uint32_t index = 0, gen = 0; };
    struct Drained { Ref ref; Kind kind; std::uint64_t obligation; SlotState st; Payload p; };
    explicit Ledger(std::size_t cap) : slots_(cap) {}

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
        if (s.gen != r.gen || s.st != SlotState::Reserved) { ++rejected_[static_cast<int>(Reject::Revoked)]; return Reject::Revoked; }
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
    void count_reject(Reject r) { std::lock_guard g(m_); ++rejected_[static_cast<int>(r)]; }
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
    std::array<std::size_t, 4> rejected_{};  // bounded diagnostics: counters only
};

class World;

// Q140/Q142: issued only by World when an obligation is taken; move-only; cannot be built from public ids.
class ReportHandle {
public:
    ReportHandle() = default;
    ReportHandle(const ReportHandle&) = delete;
    ReportHandle& operator=(const ReportHandle&) = delete;
    ReportHandle(ReportHandle&& o) noexcept : l_(std::move(o.l_)), r_(o.r_), live_(std::exchange(o.live_, false)) {}
    ReportHandle& operator=(ReportHandle&& o) noexcept {
        if (this != &o) { drop_if_live(); l_ = std::move(o.l_); r_ = o.r_; live_ = std::exchange(o.live_, false); }
        return *this;
    }
    ~ReportHandle() { drop_if_live(); }

    // Single use: after this call the handle is empty whatever the outcome.
    Reject report(Payload p = {}) {
        if (!live_) {  // used handles still know their ledger; moved-from ones do not
            if (auto l = l_.lock()) l->count_reject(Reject::Empty);
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

// Owner-thread state. Every mutating call checks the calling thread.
class World {
public:
    struct Command { int env; bool running = true; std::string terminal; int value = 0; Ledger::Ref ref; };
    struct Receipt { int env; std::string terminal; bool handedOver = false; std::optional<Ledger::Ref> ack; std::uint64_t ackOb = 0; };
    struct Outcome { std::uint64_t id; int env; std::string outcome; };
    struct Env { std::string name; Contract contract; bool closed = false, ended = false, cleanupSafe = true; int value = 0; };
    struct Part { bool active = false, lost = false; Ledger::Ref ref; std::uint64_t ob = 0; };
    struct Handoff { int env; bool acked = false; std::uint64_t assignment = 0; Part cb, work, lease; std::optional<Ledger::Ref> ack; std::uint64_t ackOb = 0; };

    explicit World(std::size_t reportCap) : ledger_(std::make_shared<Ledger>(reportCap)) {}
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Q137: an env without a valid acceptance point is refused.
    std::optional<int> add_env(std::string n, std::optional<Contract> c) {
        owner();
        if (!c || (*c != Contract::Handover && *c != Contract::ExplicitAck)) { ++refusedContracts; return std::nullopt; }
        envs.push_back({std::move(n), *c}); return static_cast<int>(envs.size() - 1);
    }
    // Q135: reserve the report place before taking the obligation; no place, no obligation.
    std::optional<ReportHandle> start_command(int env) {
        owner();
        if (envs[env].closed) return std::nullopt;
        const auto id = next();
        auto r = ledger_->reserve(Kind::Result, id);
        if (!r) { ++refusedObligations; return std::nullopt; }
        try { commands[id] = Command{env, true, {}, 0, *r}; receipts[id] = Receipt{env, {}, false, std::nullopt, 0}; }
        catch (...) { commands.erase(id); receipts.erase(id); ledger_->revoke(*r); throw; }
        return ReportHandle(ledger_, *r);
    }
    std::uint64_t fault(int env) { owner(); const auto id = next(); handoffs[id] = Handoff{env, false, 0, {}, {}, {}, std::nullopt, 0}; return id; }
    // Needs four places (callback/work/lease end + ack); all or nothing.
    std::optional<DeliveryHandles> deliver(std::uint64_t h) {
        owner();
        auto& x = handoffs.at(h);
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
        for (std::size_t i = 0; i < n; ++i) { apply(buf[i]); ledger_->complete(buf[i].ref); }
        finalize();
        return n;
    }
    void close(int env) { owner(); envs[env].closed = true; finalize(); }
    void set_cleanup_safe(int env, bool s) { owner(); envs[env].cleanupSafe = s; finalize(); }
    // Receiver takes settled results; acceptance follows the env contract (Q137).
    std::vector<ReportHandle> collect(int env) {
        owner(); std::vector<ReportHandle> acks;
        if (envs[env].closed && !envs[env].cleanupSafe) return acks;  // F5: unsafe cleanup keeps results
        acks.reserve(receipts.size());  // push_back below cannot reallocate
        for (auto it = receipts.begin(); it != receipts.end();) {
            auto& [id, r] = *it;
            if (r.env != env || r.terminal.empty() || r.handedOver) { ++it; continue; }
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
    // Fixture owner decision after a lost reporter (Q143 b): only when cleanup is declared safe.
    bool resolve_lost(std::uint64_t h) {
        owner(); auto& x = handoffs.at(h);
        if (!envs[x.env].cleanupSafe) return false;
        bool any = false;
        for (Part* p : {&x.cb, &x.work, &x.lease}) if (p->active && p->lost) { p->active = p->lost = false; any = true; }
        try_release(h); return any;
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
    std::vector<std::string> ownerNotices;  // Q143 b: reporter-lost notices
    std::size_t refusedContracts = 0, refusedObligations = 0, providerLost = 0;
    std::atomic<std::size_t> ownerThreadViolations{0};  // the only field written by non-owner threads

private:
    void owner() { if (std::this_thread::get_id() != ownerThread_) { ++ownerThreadViolations; throw std::logic_error("非擁有者執行緒"); } }
    std::uint64_t next() { return ++seq_; }
    // Grow geometrically before the push so push_back cannot reallocate (keeps record() strong).
    template <class V> static void make_room(V& v) { if (v.size() == v.capacity()) v.reserve(v.empty() ? 8 : v.size() * 2); }
    void record(std::uint64_t id, int env, const char* o) {
        Outcome x{id, env, o}; make_room(outcomes); outcomes.push_back(std::move(x));
    }
    void apply(const Ledger::Drained& d) {
        if (d.kind == Kind::Result) {
            auto& c = commands.at(d.obligation);
            auto& r = receipts.at(d.obligation);
            const bool lost = d.st == SlotState::Dropped;
            std::string t1 = lost ? "提供者失效" : d.p.cancelled ? "已取消" : "已完成", t2 = t1;
            c.running = false; c.terminal.swap(t1); r.terminal.swap(t2);
            if (lost) { ++providerLost; return; }  // Q143 a
            c.value = d.p.value;
            if (!d.p.cancelled && !envs[c.env].closed) envs[c.env].value = d.p.value;  // F4
            return;
        }
        if (d.kind == Kind::ResultAck) {
            const auto id = ackOwner.at(d.obligation);
            auto it = receipts.find(id);
            if (it != receipts.end()) {
                if (d.st == SlotState::Filled) { record(id, it->second.env, "已交付"); receipts.erase(it); }
                else { it->second.ack.reset(); it->second.handedOver = false; }  // ack handle dropped: hand over again later
            }
            ackOwner.erase(d.obligation);
            return;
        }
        const auto h = partOwner.at(d.obligation);
        auto& x = handoffs.at(h);
        if (d.kind == Kind::HandoffAck) {
            x.ack.reset();
            if (d.st == SlotState::Filled) x.acked = true;
        } else {
            Part& p = d.kind == Kind::CallbackEnd ? x.cb : d.kind == Kind::WorkEnd ? x.work : x.lease;
            if (d.st == SlotState::Filled) p.active = false;
            else {  // Q143 b: not assumed ended
                std::string notice = "回報者遺失：交接 " + std::to_string(h);
                make_room(ownerNotices);
                p.lost = true; ownerNotices.push_back(std::move(notice));
            }
        }
        partOwner.erase(d.obligation);
        try_release(h);
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
    std::map<std::uint64_t, std::uint64_t> partOwner, ackOwner;
};

}  // namespace rh
