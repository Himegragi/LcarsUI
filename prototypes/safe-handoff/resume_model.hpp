#pragma once
// THROWAWAY COMPANION MODEL v2 (v2: per-request receipts, bounded route registry): pause/resume after loss of a required receiving route (ADR 0003, route-loss paragraphs).
// Separate from the v5 host-confirmation model (handoff_model.hpp). Not proof that subsystems are integrated, not an API.
// Questions: route loss DURING recovery, continuously arriving results not blocking recovery, no duplicate application.
// Fixture/experimental choices (NOT approved policy): late results after reopen go to following bounded normal rounds;
// "required logical capacity" for resume = per-environment batch storage reserved up front; data_version changes only through the
// explicit external-change fixture (applying a result does NOT bump the source version for later results).
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace resume {

inline std::uint64_t next_id(){static std::atomic<std::uint64_t> n{0};return ++n;}   // never reused

enum class R{Ok,RefusedPaused,RefusedFaulted,RefusedNoRoute,RefusedNoCapacity,RefusedQueueFull,RefusedWrongThread,RefusedStale,
    RefusedNotPaused,RefusedNotDone,RefusedEmpty,Invalidated,RefusedUnknown};
inline const char* name(R r){switch(r){case R::Ok:return "成功";case R::RefusedPaused:return "拒絕：已暫停";case R::RefusedFaulted:return "拒絕：故障需重建";
    case R::RefusedNoRoute:return "拒絕：接收途徑無效";case R::RefusedNoCapacity:return "拒絕：容量不足";case R::RefusedQueueFull:return "拒絕：待辦已滿";
    case R::RefusedWrongThread:return "拒絕：非擁有者執行緒";case R::RefusedStale:return "拒絕：過期的恢復嘗試";case R::RefusedNotPaused:return "拒絕：不在暫停狀態";
    case R::RefusedNotDone:return "拒絕：本批尚未處理完";case R::RefusedEmpty:return "拒絕：沒有項目";case R::Invalidated:return "恢復嘗試失效";default:return "拒絕：未知";}}
enum class EnvState{Running,Paused,Resuming,Faulted};
inline const char* name(EnvState s){switch(s){case EnvState::Running:return "執行中";case EnvState::Paused:return "已暫停";case EnvState::Resuming:return "恢復處理中（仍暫停普通操作）";default:return "故障（需重建）";}}
enum class Status{None,Completed,Cancelled};            // command terminal status (worker-reported)
enum class Disp{None,Applied,NotApplicable,NoResult};    // UI disposition of the retained result
inline const char* name(Disp d){switch(d){case Disp::Applied:return "已套用";case Disp::NotApplicable:return "不適用";case Disp::NoResult:return "無結果";default:return "未處置";}}

struct Token{std::uint64_t world=0,result=0;std::uint32_t slot=0,gen=0;};
struct Slot{enum S{Free,Running,Published,Settled};
    std::uint32_t gen=1;S s=Free;std::uint64_t result=0;int env=-1;unsigned target_gen=0,data_version=0;int value=0;
    bool allow_continue=false,cancel_requested=false;Status status=Status::None;};
// Worker side touches ONLY this (token + publish). Publication goes into the slot reserved at start: no extra storage.
struct Ledger{
    std::mutex m;std::uint64_t world;std::vector<Slot> slots;int published=0,rejected=0;   // published/rejected guarded by m
    Ledger(std::uint64_t w,size_t cap):world(w),slots(cap){}
    bool publish(const Token& t,int value,bool cancelled=false){std::lock_guard l(m);
        if(t.world!=world||t.slot>=slots.size()){++rejected;return false;}
        Slot& s=slots[t.slot];if(s.gen!=t.gen||s.result!=t.result||s.s!=Slot::Running){++rejected;return false;}
        s.s=Slot::Published;s.value=value;s.status=cancelled?Status::Cancelled:Status::Completed;++published;return true;}
};
struct Route{std::uint64_t id=0;std::string name;unsigned version=1;bool alive=true;};
struct Env{std::string name;EnvState state=EnvState::Running;std::uint64_t route=0;unsigned route_version=0;unsigned target_gen=1,data_version=1;
    int value=0;std::uint64_t last_result=0;std::deque<std::uint64_t> queue;std::uint64_t attempt=0;
    int applied=0,not_applicable=0,not_executed=0,cancel_requests=0,cleanups=0;};
struct Item{std::uint32_t slot=0,gen=0;std::uint64_t result=0;Disp disp=Disp::None;};
struct Attempt{std::uint64_t id=0;std::uint64_t route=0;unsigned route_version=0;std::vector<Item> batch;size_t cursor=0;bool live=false;};
struct Record{std::uint64_t result=0;Disp disp=Disp::None;int applied_count=0;};   // bounded trace ring, not used for correctness
// v2: per-request terminal receipt (command/action terminal status), separate from retained UI ownership/disposition.
// Each accepted action/command reserves one receipt credit at admission; the credit returns when the caller takes it.
// ui_pending: snapshot taken at terminal settlement ("UI disposition still pending then"); it is NOT updated later.
struct Receipt{enum K{NotExecuted,Completed,Cancelled} kind;std::uint64_t id=0;int env=-1;bool ui_pending=false;};
inline const char* name(Receipt::K k){switch(k){case Receipt::NotExecuted:return "未執行";case Receipt::Completed:return "已完成";default:return "已取消";}}

struct World{
    static constexpr size_t slot_cap=6,queue_cap=3,env_cap=2,trace_cap=16,log_cap=120,route_cap=4,receipt_cap=slot_cap+env_cap*queue_cap;
    std::deque<Receipt> outbox;size_t receipt_credits=0;unsigned route_seq=0;
    const std::uint64_t id=next_id();std::thread::id owner=std::this_thread::get_id();
    std::shared_ptr<Ledger> ledger=std::make_shared<Ledger>(id,slot_cap);
    std::vector<Route> routes;std::vector<Env> envs;std::vector<Attempt> attempts;   // attempts[i] belongs to envs[i]
    std::deque<Record> trace;std::deque<std::string> log;
    struct C{int reserved=0,freed=0,applied=0,not_applicable=0,no_result=0,accepted_actions=0,started_from_queue=0,not_executed=0,
        refused=0,settled=0,attempts=0,invalidated=0,commits=0,double_apply=0,stale_items=0,normal_rounds=0;} c;

    bool on_owner() const{return std::this_thread::get_id()==owner;}
    void L(const std::string& s){if(log.size()>=log_cap)log.pop_front();log.push_back(s);}
    Env* env(const std::string& n){for(auto& e:envs)if(e.name==n)return &e;return nullptr;}
    int idx(const Env* e) const{return e?int(e-envs.data()):-1;}
    Route* route(std::uint64_t r){for(auto& x:routes)if(x.id==r)return &x;return nullptr;}
    size_t used(){std::lock_guard l(ledger->m);size_t n=0;for(auto& s:ledger->slots)n+=s.s!=Slot::Free;return n;}
    size_t count(int e,Slot::S st){std::lock_guard l(ledger->m);size_t n=0;for(auto& s:ledger->slots)n+=s.env==e&&s.s==st;return n;}
    size_t retained(int e){return count(e,Slot::Settled);}
    // Capacity / disposition invariants.
    bool invariant(){const size_t u=used();if(u>slot_cap||int(u)!=c.reserved-c.freed)return false;
        if(c.applied+c.not_applicable+c.no_result!=c.freed)return false;
        int queued=0;for(auto& e:envs){queued+=int(e.queue.size());if(e.queue.size()>queue_cap)return false;
            if(e.state==EnvState::Running&&e.attempt)return false;if(e.state!=EnvState::Running&&!e.queue.empty())return false;}
        if(routes.size()>route_cap||outbox.size()>receipt_cap)return false;
        // every receipt credit is an unreceipted obligation (queued action, running/published command) or an untaken receipt
        if(receipt_credits!=size_t(queued)+count_all(Slot::Running)+count_all(Slot::Published)+outbox.size())return false;
        return c.accepted_actions==c.started_from_queue+c.not_executed+queued&&c.double_apply==0;}
    std::vector<Receipt> take_receipts(){std::vector<Receipt> r;if(!on_owner())return r;r.assign(outbox.begin(),outbox.end());receipt_credits-=outbox.size();outbox.clear();return r;}

    // ---- routes (bounded registry: dead, unreferenced entries are reclaimed; ids never reused) ----
    std::uint64_t add_route(const std::string& n){if(!on_owner())return 0;Route* slot=nullptr;
        if(routes.size()<route_cap){routes.emplace_back();slot=&routes.back();}
        else for(auto& r:routes)if(!r.alive&&!referenced(r.id)){slot=&r;break;}
        if(!slot){++c.refused;L("拒絕安裝途徑 "+n+"：途徑表已滿（仍被引用）");return 0;}
        *slot={next_id(),n,++route_seq,true};L("安裝接收途徑 "+n+" #"+std::to_string(slot->id)+" 版本"+std::to_string(slot->version)+"（不會自動恢復）");return slot->id;}
    R lose_route(std::uint64_t r){if(!on_owner())return R::RefusedWrongThread;Route* x=route(r);if(!x||!x->alive)return R::RefusedNoRoute;x->alive=false;
        L("接收途徑 #"+std::to_string(r)+" 失效");
        for(auto& e:envs)if(e.route==r){if(e.state==EnvState::Running)pause(e);else if(e.state==EnvState::Resuming)invalidate(e,"恢復途中接收途徑失效");}
        return R::Ok;}
    // ---- environments / ordinary operations ----
    R add_env(const std::string& n,std::uint64_t r){if(!on_owner())return R::RefusedWrongThread;Route* x=route(r);if(!x||!x->alive||envs.size()>=env_cap||env(n))return R::RefusedNoRoute;
        Env e;e.name=n;e.route=r;e.route_version=x->version;envs.push_back(e);attempts.emplace_back();attempts.back().batch.reserve(slot_cap);return R::Ok;}
    R submit(const std::string& n){if(!on_owner())return R::RefusedWrongThread;Env* e=env(n);if(!e)return R::RefusedUnknown;
        if(e->state!=EnvState::Running){++c.refused;return e->state==EnvState::Faulted?R::RefusedFaulted:R::RefusedPaused;}
        if(e->queue.size()>=queue_cap){++c.refused;return R::RefusedQueueFull;}
        if(receipt_credits>=receipt_cap){++c.refused;return R::RefusedNoCapacity;}
        last_action=next_id();e->queue.push_back(last_action);++receipt_credits;++c.accepted_actions;return R::Ok;}
    std::uint64_t last_action=0;   // id of the most recently accepted action (caller's request identity)
    R start(const std::string& n,bool allow_continue,Token& t){if(!on_owner())return R::RefusedWrongThread;Env* e=env(n);if(!e)return R::RefusedUnknown;
        if(e->state!=EnvState::Running){++c.refused;return e->state==EnvState::Faulted?R::RefusedFaulted:R::RefusedPaused;}
        if(receipt_credits>=receipt_cap){++c.refused;return R::RefusedNoCapacity;}
        auto r=reserve(*e,allow_continue,t,next_id());if(r==R::Ok)++receipt_credits;return r;}
    // A queued action keeps its identity and its receipt credit when it starts (command id == action id).
    R run_queued(const std::string& n,Token& t){if(!on_owner())return R::RefusedWrongThread;Env* e=env(n);if(!e||e->queue.empty())return R::RefusedEmpty;
        if(e->state!=EnvState::Running)return R::RefusedPaused;
        auto r=reserve(*e,false,t,e->queue.front());if(r==R::Ok){e->queue.pop_front();++c.started_from_queue;}return r;}
    R fault(const std::string& n){if(!on_owner())return R::RefusedWrongThread;Env* e=env(n);if(!e)return R::RefusedUnknown;
        if(e->state==EnvState::Running)pause(*e);if(e->attempt)invalidate(*e,"環境故障");e->state=EnvState::Faulted;L(n+" 故障：不能恢復，只能另行重建");return R::Ok;}
    R safe_cleanup(const std::string& n){if(!on_owner())return R::RefusedWrongThread;Env* e=env(n);if(!e)return R::RefusedUnknown;++e->cleanups;return R::Ok;}   // always available
    // Fixture conditions: an external source change (version bump) and a target-generation change.
    R external_change(const std::string& n){if(!on_owner())return R::RefusedWrongThread;Env* e=env(n);if(!e)return R::RefusedUnknown;++e->data_version;L(n+" 外部資料版本變更 → "+std::to_string(e->data_version));return R::Ok;}
    R retarget(const std::string& n){if(!on_owner())return R::RefusedWrongThread;Env* e=env(n);if(!e)return R::RefusedUnknown;++e->target_gen;L(n+" 目標世代變更 → "+std::to_string(e->target_gen));return R::Ok;}

    // ---- owner settles publications: terminal status visible, result retained (never applied here) ----
    int drain(){if(!on_owner())return 0;int n=0;std::lock_guard l(ledger->m);
        for(auto& s:ledger->slots)if(s.s==Slot::Published){++n;++c.settled;
            if(s.status==Status::Cancelled){outbox.push_back({Receipt::Cancelled,s.result,s.env,false});free_locked(s,Disp::NoResult);continue;}   // no UI payload
            outbox.push_back({Receipt::Completed,s.result,s.env,true});s.s=Slot::Settled;}   // terminal now; UI disposition still pending
        return n;}
    // Experimental rule: results of Running environments are handled in bounded normal rounds (also late arrivals).
    int normal_round(int budget){if(!on_owner())return 0;int n=0;++c.normal_rounds;std::lock_guard l(ledger->m);
        for(auto& s:ledger->slots){if(n>=budget)break;if(s.s!=Slot::Settled)continue;Env& e=envs[s.env];if(e.state!=EnvState::Running)continue;dispose_locked(s,e);++n;}
        return n;}

    // ---- explicit resume ----
    std::optional<std::uint64_t> begin_resume(const std::string& n,std::uint64_t r){if(!on_owner())return std::nullopt;Env* e=env(n);
        auto refuse=[&](const char* why){++c.refused;L(n+" 恢復請求被拒："+why);return std::optional<std::uint64_t>{};};
        if(!e)return refuse("無此環境");if(e->state==EnvState::Faulted)return refuse("故障實例不能恢復（需重建）");
        if(e->state!=EnvState::Paused)return refuse("不在暫停狀態");
        Route* x=route(r);if(!x||!x->alive)return refuse("接收途徑無效");
        // Required logical capacity (fixture): the batch storage is reserved per environment at add_env (slot_cap items),
        // so every retained result always fits. Requiring a FREE slot would deadlock when retention is full.
        Attempt& a=attempts[idx(e)];a.id=next_id();a.route=r;a.route_version=x->version;a.batch.clear();a.cursor=0;a.live=true;
        {std::lock_guard l(ledger->m);for(std::uint32_t i=0;i<ledger->slots.size();++i){auto& s=ledger->slots[i];
            if(s.s==Slot::Settled&&s.env==idx(e))a.batch.push_back({i,s.gen,s.result});}}
        e->route=r;e->route_version=x->version;e->state=EnvState::Resuming;e->attempt=a.id;++c.attempts;
        L(n+" 開始恢復嘗試 #"+std::to_string(a.id)+"：固定本批 "+std::to_string(a.batch.size())+" 份結果");return a.id;}
    R resume_step(std::uint64_t aid,int budget){if(!on_owner())return R::RefusedWrongThread;Env* e=by_attempt(aid);if(!e)return R::RefusedStale;
        Attempt& a=attempts[idx(e)];int n=0;
        while(n<budget&&a.cursor<a.batch.size()){
            if(!conditions(*e,a)){invalidate(*e,"處理前檢查失敗");return R::Invalidated;}   // checked before EACH apply
            Item& it=a.batch[a.cursor];std::lock_guard l(ledger->m);Slot& s=ledger->slots[it.slot];
            if(s.gen==it.gen&&s.result==it.result&&s.s==Slot::Settled)it.disp=dispose_locked(s,*e);else ++c.stale_items;
            ++a.cursor;++n;}
        return R::Ok;}
    R commit_resume(std::uint64_t aid){if(!on_owner())return R::RefusedWrongThread;Env* e=by_attempt(aid);if(!e)return R::RefusedStale;
        Attempt& a=attempts[idx(e)];if(a.cursor<a.batch.size())return R::RefusedNotDone;
        if(!conditions(*e,a)){invalidate(*e,"提交時檢查失敗");return R::Invalidated;}
        e->state=EnvState::Running;e->attempt=0;a.live=false;++c.commits;
        L(e->name+" 提交恢復 #"+std::to_string(aid)+" → 重新開放普通操作；仍留存的晚到結果 "+std::to_string(retained(idx(e)))+" 份交給後續一般處理輪");return R::Ok;}
    Attempt* attempt_of(const std::string& n){Env* e=env(n);return e?&attempts[idx(e)]:nullptr;}
private:
    Env* by_attempt(std::uint64_t aid){if(!aid)return nullptr;for(size_t i=0;i<envs.size();++i)if(envs[i].attempt==aid&&attempts[i].live&&attempts[i].id==aid&&envs[i].state==EnvState::Resuming)return &envs[i];return nullptr;}
    bool conditions(Env& e,const Attempt& a){Route* x=route(a.route);return x&&x->alive&&x->version==a.route_version&&e.route==a.route&&e.state==EnvState::Resuming;}
    void invalidate(Env& e,const char* why){Attempt& a=attempts[idx(&e)];a.live=false;e.attempt=0;if(e.state==EnvState::Resuming)e.state=EnvState::Paused;++c.invalidated;
        L(e.name+" 恢復嘗試 #"+std::to_string(a.id)+" 失效（"+why+"）：維持暫停；已處置結果保持終態");}
    void pause(Env& e){e.state=EnvState::Paused;
        while(!e.queue.empty()){outbox.push_back({Receipt::NotExecuted,e.queue.front(),idx(&e),false});e.queue.pop_front();++e.not_executed;++c.not_executed;}   // one receipt each
        std::lock_guard l(ledger->m);for(auto& s:ledger->slots)if(s.env==idx(&e)&&s.s==Slot::Running&&!s.allow_continue&&!s.cancel_requested){s.cancel_requested=true;++e.cancel_requests;}
        L(e.name+" 暫停：普通操作拒絕；未開始的動作回報未執行；已開始工作預設提出取消請求（保留預留）");}
    bool referenced(std::uint64_t r) const{for(size_t i=0;i<envs.size();++i)if(envs[i].route==r||(attempts[i].live&&attempts[i].route==r))return true;return false;}
    size_t count_all(Slot::S st){std::lock_guard l(ledger->m);size_t n=0;for(auto& s:ledger->slots)n+=s.s==st;return n;}
    R reserve(Env& e,bool allow,Token& t,std::uint64_t cmd){std::lock_guard l(ledger->m);
        for(std::uint32_t i=0;i<ledger->slots.size();++i){Slot& s=ledger->slots[i];if(s.s!=Slot::Free)continue;
            s.s=Slot::Running;s.result=cmd;s.env=idx(&e);s.target_gen=e.target_gen;s.data_version=e.data_version;s.value=0;s.allow_continue=allow;s.cancel_requested=false;s.status=Status::None;
            t={id,s.result,i,s.gen};++c.reserved;return R::Ok;}
        ++c.refused;return R::RefusedNoCapacity;}
    Disp dispose_locked(Slot& s,Env& e){Disp d;
        if(s.target_gen==e.target_gen&&s.data_version==e.data_version){e.value=s.value;e.last_result=s.result;++e.applied;++c.applied;d=Disp::Applied;}
        else{++e.not_applicable;++c.not_applicable;d=Disp::NotApplicable;}
        for(auto& r:trace)if(r.result==s.result)++c.double_apply;   // would be a second disposition of the same result
        if(trace.size()>=trace_cap)trace.pop_front();trace.push_back({s.result,d,d==Disp::Applied?1:0});
        free_locked(s,d);return d;}
    // Ownership returns only here: after a terminal UI disposition (or NoResult for a cancelled command).
    void free_locked(Slot& s,Disp d){if(d==Disp::NoResult)++c.no_result;s.s=Slot::Free;++s.gen;s.result=0;s.env=-1;++c.freed;}
};

} // namespace resume
