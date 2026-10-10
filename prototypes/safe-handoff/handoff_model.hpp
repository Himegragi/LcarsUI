#pragma once
// THROWAWAY SAFE-HANDOFF LOGIC PROTOTYPE v4 (prototypes/safe-handoff). Not framework code or an API proposal.
// v4 models explicit host acknowledgment and reassignment of a required fault handoff:
//  - an active receiving route must exist before an environment is enabled;
//  - only an explicit VALID confirmation accepts responsibility (callback return is not acknowledgment);
//  - each handoff and each assignment has a distinct never-reused identity;
//  - reassignment acceptance immediately revokes old authority; activation is an explicit owner-thread commit that
//    waits (non-blocking) for the old assignment's tracked callbacks/work; cancel vs activate = first legal transition;
//  - handoff credit is released once, only after valid confirmation AND safe callback completion AND safe-release.
// Safety (handoff path, cleanup, post-throw callback state) is DECLARED fixture input, not detection.
// Out of scope: fatal paths, process termination, memory corruption, GPU, IME, UIA.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace handoff {

inline std::uint64_t next_unique(){static std::atomic<std::uint64_t> n{0};return ++n;}  // never reused (ids start at 1)

// ---- bounded async result ledger (worker side touches only this, never UI objects) ----
struct Token { std::uint64_t ledger=0,instance=0; std::uint32_t slot=0,gen=0; unsigned target_gen=0; bool valid() const{return gen!=0;} };
struct Ledger {
    enum class S{Free,Reserved,Published};
    struct Slot{std::uint32_t gen=1;S s=S::Free;std::uint64_t instance=0;unsigned target_gen=0;int value=0;};
    mutable std::mutex m; const std::uint64_t id=next_unique(); std::vector<Slot> slots;
    int rejected_publish=0,published=0,released=0;   // guarded by m
    explicit Ledger(size_t cap):slots(cap){}
    bool reserve(std::uint64_t inst,unsigned tgen,Token& t){std::lock_guard l(m);
        for(std::uint32_t i=0;i<slots.size();++i)if(slots[i].s==S::Free){slots[i]={slots[i].gen,S::Reserved,inst,tgen,0};t={id,inst,i,slots[i].gen,tgen};return true;}return false;}
    bool publish(const Token& t,int value){std::lock_guard l(m);
        if(t.ledger!=id||t.slot>=slots.size()){++rejected_publish;return false;}
        auto& s=slots[t.slot];
        if(s.gen!=t.gen||s.s!=S::Reserved||s.instance!=t.instance){++rejected_publish;return false;}
        s.s=S::Published;s.value=value;++published;return true;}
    template<class F> int settle_all(F f){int n=0;std::vector<std::pair<std::uint32_t,Slot>> ready;
        {std::lock_guard l(m);for(std::uint32_t i=0;i<slots.size();++i)if(slots[i].s==S::Published)ready.push_back({i,slots[i]});}
        for(auto& [i,s]:ready){f(s);std::lock_guard l(m);slots[i].s=S::Free;++slots[i].gen;++released;++n;}return n;}
    size_t used() const{std::lock_guard l(m);size_t n=0;for(auto& s:slots)n+=s.s!=S::Free;return n;}
    size_t published_waiting() const{std::lock_guard l(m);size_t n=0;for(auto& s:slots)n+=s.s==S::Published;return n;}
};

enum class InstanceState{Running,Unavailable};
struct Instance {
    std::uint64_t id; std::string env; unsigned gen; InstanceState state=InstanceState::Running;
    std::vector<std::string> services; std::uint64_t fault_seq=0; std::string fault;
    unsigned target_gen=1; int value=0; size_t reservations=0; bool cleanup_safe=true,cleaned=false;
};
struct Service { std::string name; unsigned gen=1; bool trustworthy=true; };

struct Notification { std::uint64_t handoff; std::string reason; std::vector<std::string> envs; std::vector<std::uint64_t> instances; std::vector<unsigned> gens; };
enum class HostReply{KeepProcess,RequestExit};
enum class ActionResult{Ok,RefusedUnavailable,RefusedInHostCallback,RefusedNoInstance,RefusedOutOfScope,RefusedWrongThread,RefusedNotSafePoint,
    RefusedNoCapacity,RefusedDuplicate,RefusedNoReceiver,RefusedInvalid,RefusedStale,RefusedRevoked,RefusedPending,RefusedNotEligible,RefusedDependents,TooLate,Failed};
inline const char* name(ActionResult r){switch(r){
    case ActionResult::Ok:return "成功";case ActionResult::RefusedUnavailable:return "拒絕：不可用";case ActionResult::RefusedInHostCallback:return "拒絕：宿主回呼中";
    case ActionResult::RefusedNoInstance:return "拒絕：無實例";case ActionResult::RefusedOutOfScope:return "拒絕：超出安全交接範圍";case ActionResult::RefusedWrongThread:return "拒絕：非擁有者執行緒";
    case ActionResult::RefusedNotSafePoint:return "拒絕：不在安全點";case ActionResult::RefusedNoCapacity:return "拒絕：容量不足";case ActionResult::RefusedDuplicate:return "拒絕：重複";
    case ActionResult::RefusedNoReceiver:return "拒絕：沒有有效接收者";case ActionResult::RefusedInvalid:return "拒絕：身分無效";case ActionResult::RefusedStale:return "拒絕：過期";
    case ActionResult::RefusedRevoked:return "拒絕：資格已撤銷";case ActionResult::RefusedPending:return "拒絕：已有未完成改派";case ActionResult::RefusedNotEligible:return "拒絕：尚未具備生效條件";
    case ActionResult::RefusedDependents:return "拒絕：仍有相依環境";case ActionResult::TooLate:return "太晚：已生效";default:return "失敗";}}

struct FaultSpec { bool state_still_valid=false; std::string service; bool dependency_known=true; bool handoff_path_safe=true; std::string reason="extension threw"; };
enum class DiagMode{Enabled,Disabled,Full,Throwing};

struct World;
// Confirmation authority = (world, handoff, assignment). WorkToken tracks host work started under an assignment.
struct AckToken { std::uint64_t world=0,handoff=0,assignment=0; };
struct WorkToken { std::uint64_t world=0,handoff=0,assignment=0,work=0; };   // work: unique per begin_work, never reused
using Callback=std::function<HostReply(World&,const Notification&,const AckToken&)>;
struct Receiver { std::uint64_t id; std::string name; unsigned version=1; bool post_throw_safe=true; Callback cb; };

// jobs: bounded set of outstanding tracked-work ids (no completed history); work == number of nonzero entries.
struct Assignment { std::uint64_t id=0,receiver=0; bool authority=false,callback_active=false,route_failed=false; int work=0; std::array<std::uint64_t,4> jobs{}; };
struct Reassign { enum S{None,Waiting,Activated,Cancelled,Failed} s=None; std::uint64_t id=0,candidate=0; unsigned candidate_version=0; };
inline const char* name(Reassign::S s){switch(s){case Reassign::None:return "無";case Reassign::Waiting:return "等待生效";case Reassign::Activated:return "已生效";case Reassign::Cancelled:return "已取消";default:return "已失敗";}}
struct Handoff {
    enum Phase{Free,AwaitingDelivery,AwaitingAck,AwaitingTakeover,Confirmed};
    std::uint64_t id=0; Phase phase=Free; Notification note; size_t credits=0;
    std::array<Assignment,4> asg{}; std::uint64_t current=0; Reassign req;
    bool release_safe=true,failed_after_ack=false;
    Assignment* find(std::uint64_t a){for(auto& x:asg)if(x.id&&x.id==a)return &x;return nullptr;}
    Assignment* cur(){return find(current);}
    // Before activation every recorded assignment (including the revoked current one) is "old".
    bool old_work_ended() const{for(auto& x:asg)if(x.id&&(x.work>0||x.callback_active))return false;return true;}
    bool callbacks_ended() const{for(auto& x:asg)if(x.id&&x.callback_active)return false;return true;}
};
inline const char* name(Handoff::Phase p){switch(p){case Handoff::Free:return "空";case Handoff::AwaitingDelivery:return "待送出";case Handoff::AwaitingAck:return "待確認";
    case Handoff::AwaitingTakeover:return "待接管";default:return "已確認";}}

// Bounded owner-thread control mailbox: workers post control messages, never touch World directly.
struct Control { enum Kind{Confirm,Cancel,Activate,EndWork} kind; AckToken ack; std::uint64_t handoff=0,request=0; WorkToken work; };
struct ControlBox { std::mutex m; std::deque<Control> q; static constexpr size_t cap=16;
    bool post(const Control& c){std::lock_guard l(m);if(q.size()>=cap)return false;q.push_back(c);return true;}
    bool pop(Control& c){std::lock_guard l(m);if(q.empty())return false;c=q.front();q.pop_front();return true;} };

struct World {
    static constexpr size_t instance_cap=4,notice_cap=4,log_cap=120,diag_cap=4,receiver_cap=6;
    const std::uint64_t id=next_unique();
    std::thread::id owner=std::this_thread::get_id();
    std::shared_ptr<Ledger> ledger=std::make_shared<Ledger>(6);
    std::shared_ptr<ControlBox> control=std::make_shared<ControlBox>();
    std::map<std::string,Service> services; std::map<std::string,bool> isolated;
    std::vector<Instance> instances; std::map<std::string,std::uint64_t> current;
    std::array<Handoff,notice_cap> handoffs{}; size_t notice_used=0;   // Running instances + unreleased handoff credits
    std::map<std::uint64_t,Receiver> receivers; std::uint64_t active_receiver=0;
    std::deque<std::string> log; std::vector<std::string> diag; DiagMode diag_mode=DiagMode::Enabled; bool diag_disabled_after_throw=false;
    int depth=0,traversals=0; bool in_host_callback=false; std::uint64_t callback_handoff=0;
    bool out_of_scope=false,process_exit_requested=false;
    struct Counters{int delivered=0,callbacks=0,handoff_failed=0,failed_after_ack=0,deferred_delivery=0,refused_reentry=0,refused_actions=0,repeated_fault_attempts=0,
        late_rejected_unavailable=0,late_rejected_generation=0,late_rejected_missing=0,late_applied=0,diag_failures=0,rebuilds=0,rebuilds_refused=0,retired=0,
        held_out_of_scope=0,admissions_refused=0,confirmed=0,confirm_rejected=0,released=0,reassign_accepted=0,reassign_refused=0,activated=0,cancelled=0,
        reassign_failed=0,too_late=0,receiver_lost=0,control_processed=0;} c;

    bool on_owner() const{return std::this_thread::get_id()==owner;}
    void L(const std::string& s){if(log.size()>=log_cap)log.pop_front();log.push_back(s);}
    void diagnose(const std::string& s){
        if(diag_mode==DiagMode::Disabled||diag_disabled_after_throw)return;
        try{if(diag_mode==DiagMode::Throwing)throw std::runtime_error("diagnostic sink failed");
            if(diag.size()>=diag_cap){++c.diag_failures;return;}diag.push_back(s);}
        catch(const std::exception&){++c.diag_failures;diag_disabled_after_throw=true;L("診斷接收器拋出例外 → 停用該接收器");}
    }
    Instance* find(std::uint64_t i){for(auto& x:instances)if(x.id==i)return &x;return nullptr;}
    Instance* cur(const std::string& env){auto it=current.find(env);return it==current.end()?nullptr:find(it->second);}
    Handoff* handoff(std::uint64_t h){if(!h)return nullptr;for(auto& x:handoffs)if(x.id==h)return &x;return nullptr;}
    size_t running() const{size_t n=0;for(auto& i:instances)n+=i.state==InstanceState::Running;return n;}
    // Capacity equation checked by scenarios after every transition.
    bool invariant() const{size_t held=0,n=0;for(auto& h:handoffs)if(h.id){held+=h.credits;++n;}return notice_used==running()+held&&notice_used<=notice_cap&&n<=notice_cap;}
    size_t held_credits() const{size_t n=0;for(auto& h:handoffs)if(h.id)n+=h.credits;return n;}

    ActionResult mutation_gate(){if(!on_owner())return ActionResult::RefusedWrongThread;
        if(in_host_callback){++c.refused_reentry;++c.refused_actions;return ActionResult::RefusedInHostCallback;}return ActionResult::Ok;}
    ActionResult structural_gate(){auto g=mutation_gate();if(g!=ActionResult::Ok)return g;if(depth>0||traversals>0){++c.refused_actions;return ActionResult::RefusedNotSafePoint;}return g;}
    bool receiver_alive(std::uint64_t r) const{auto it=receivers.find(r);return r&&it!=receivers.end()&&static_cast<bool>(it->second.cb);}

    // ---- receivers ----
    // An empty callback is not a receiving route (a callback that returns without confirming is still valid).
    std::uint64_t add_receiver(const std::string& nm,Callback cb,bool post_throw_safe=true){
        if(!cb||structural_gate()!=ActionResult::Ok||receivers.size()>=receiver_cap)return 0;
        const auto rid=next_unique();receivers[rid]={rid,nm,1,post_throw_safe,std::move(cb)};L("登記接收者 "+nm+"（身分 #"+std::to_string(rid)+"）");return rid;}
    ActionResult set_active_receiver(std::uint64_t r){auto g=structural_gate();if(g!=ActionResult::Ok)return g;if(!receiver_alive(r))return ActionResult::RefusedNoReceiver;
        active_receiver=r;L("主動接收途徑設為 "+receivers[r].name+" #"+std::to_string(r));return g;}
    void bump_receiver_version(std::uint64_t r){if(on_owner()&&receivers.count(r)){++receivers[r].version;L("接收者 #"+std::to_string(r)+" 版本變更");}}
    // Planned removal (fixture): refused while it is the active route with live environments, or while it still
    // holds authority, a running callback or tracked work for any unresolved handoff.
    ActionResult detach_receiver(std::uint64_t r){auto g=structural_gate();if(g!=ActionResult::Ok)return g;if(!receiver_alive(r))return ActionResult::RefusedNoReceiver;
        if(r==active_receiver&&running()>0){L("拒絕撤除接收者：仍有執行中的相依環境");return ActionResult::RefusedDependents;}
        for(auto& h:handoffs)if(h.id)for(auto& a:h.asg)if(a.id&&a.receiver==r&&(a.authority||a.callback_active||a.work>0)){L("拒絕撤除接收者：仍承擔未完成交接");return ActionResult::RefusedDependents;}
        if(r==active_receiver)active_receiver=0;receivers.erase(r);L("計畫性撤除接收者 #"+std::to_string(r));return g;}
    // Unexpected loss: a failure, not a successful detach.
    void receiver_lost(std::uint64_t r){if(!on_owner()||!receivers.count(r))return;receivers.erase(r);++c.receiver_lost;L("接收者 #"+std::to_string(r)+" 意外失效（失敗）");
        if(r==active_receiver)active_receiver=0;
        for(auto& h:handoffs){if(!h.id)continue;
            if(h.req.s==Reassign::Waiting&&h.req.candidate==r){h.req.s=Reassign::Failed;++c.reassign_failed;L("交接 #"+std::to_string(h.id)+" 的改派失敗：候選失效；維持待接管、保留額度");}
            if(auto* a=h.cur();a&&a->receiver==r&&h.phase!=Handoff::Confirmed){a->route_failed=true;a->authority=false;h.phase=Handoff::AwaitingTakeover;}}}

    // ---- environments ----
    ActionResult add_env(const std::string& env,std::vector<std::string> svc,bool iso){
        auto g=structural_gate();if(g!=ActionResult::Ok)return g;
        if(out_of_scope)return ActionResult::RefusedOutOfScope;
        if(!receiver_alive(active_receiver)){++c.admissions_refused;L("拒絕啟用 "+env+"：沒有有效的主動接收途徑");return ActionResult::RefusedNoReceiver;}
        if(current.count(env)||isolated.count(env)){++c.refused_actions;return ActionResult::RefusedDuplicate;}
        if(instances.size()>=instance_cap||notice_used>=notice_cap){++c.admissions_refused;L("拒絕新增 "+env+"：實例或交接容量不足");return ActionResult::RefusedNoCapacity;}
        for(auto& s:svc)services.try_emplace(s,Service{s});
        isolated[env]=iso;instances.push_back({next_unique(),env,1,InstanceState::Running,svc});current[env]=instances.back().id;++notice_used;L("建立 "+env+" 世代1");return g;}
    ActionResult gate(const std::string& env){auto g=mutation_gate();if(g!=ActionResult::Ok)return g;
        if(out_of_scope){++c.refused_actions;return ActionResult::RefusedOutOfScope;}
        Instance* i=cur(env);if(!i){++c.refused_actions;return ActionResult::RefusedNoInstance;}
        if(i->state!=InstanceState::Running){++c.refused_actions;return ActionResult::RefusedUnavailable;}return ActionResult::Ok;}
    ActionResult set_value(const std::string& env,int v){auto g=gate(env);if(g!=ActionResult::Ok)return g;cur(env)->value=v;return g;}
    ActionResult start_op(const std::string& env,Token& t){auto g=gate(env);if(g!=ActionResult::Ok)return g;Instance* i=cur(env);
        if(!ledger->reserve(i->id,i->target_gen,t))return ActionResult::RefusedNoCapacity;++i->reservations;L(env+" 啟動非同步工作（預留結果槽位）");return g;}
    ActionResult rebuild_target(const std::string& env){auto g=gate(env);if(g!=ActionResult::Ok)return g;Instance* i=cur(env);++i->target_gen;i->value=0;return g;}
    ActionResult dispatch(const std::string& env,const std::function<void()>& cb,const FaultSpec& spec){
        auto g=gate(env);
        if(g==ActionResult::RefusedUnavailable){++c.repeated_fault_attempts;return g;}
        if(g!=ActionResult::Ok)return g;
        ++depth;bool threw=false;std::string what;
        try{cb();}catch(const std::exception& e){threw=true;what=e.what();}catch(...){threw=true;what="unknown";}
        --depth;
        if(!threw)return ActionResult::Ok;
        diagnose(env+" 回呼例外："+what);
        if(spec.state_still_valid){L(env+" 回呼失敗，但宣告必要狀態仍有效 → 只回報失敗");return ActionResult::Failed;}
        fault(env,spec);return ActionResult::Failed;
    }
    ActionResult service_fault(const std::string& svc,bool handoff_safe=true){auto g=mutation_gate();if(g!=ActionResult::Ok)return g;
        if(out_of_scope)return ActionResult::RefusedOutOfScope;
        FaultSpec s;s.service=svc;s.handoff_path_safe=handoff_safe;s.reason="shared service "+svc+" untrustworthy";
        std::string first;for(auto& i:instances)if(current[i.env]==i.id&&std::count(i.services.begin(),i.services.end(),svc)){first=i.env;break;}
        if(first.empty())return ActionResult::RefusedNoInstance;
        if(!fault(first,s)&&!out_of_scope)return ActionResult::RefusedUnavailable;   // repeated report: counted, nothing allocated
        return g;}

    // ---- host handoff delivery: owner thread, outside callbacks and traversals, only on a declared-safe path ----
    int deliver(){
        if(!on_owner())return 0;
        if(out_of_scope){bool any=false;for(auto& h:handoffs)any|=h.id!=0;if(any)L("超出安全交接範圍：保留所有交接與額度，不呼叫宿主");return 0;}
        if(depth>0||traversals>0||in_host_callback){++c.deferred_delivery;return 0;}
        int n=0;for(auto& h:handoffs){if(out_of_scope)break;   // an unsafe callback failure stops this pass
            if(h.id&&h.phase==Handoff::AwaitingDelivery){deliver_one(h);++n;}}
        cleanup();return n;
    }
    // ---- confirmation: a narrow control operation, allowed inside the host callback ----
    ActionResult confirm(const AckToken& t){
        if(!on_owner())return ActionResult::RefusedWrongThread;
        if(out_of_scope){++c.confirm_rejected;return ActionResult::RefusedOutOfScope;}
        auto reject=[&](ActionResult r,const char* why){++c.confirm_rejected;L(std::string("確認被拒：")+why);return r;};
        if(t.world!=id)return reject(ActionResult::RefusedInvalid,"不同 World 的確認");
        Handoff* h=handoff(t.handoff);if(!h)return reject(ActionResult::RefusedStale,"交接不存在或已釋放");
        if(h->phase==Handoff::Confirmed)return reject(ActionResult::RefusedDuplicate,"重複確認");
        Assignment* a=h->find(t.assignment);   // revoked records may already be reclaimed: still "revoked", never accepted
        if(t.assignment!=h->current||!a||!a->authority||a->route_failed)return reject(ActionResult::RefusedRevoked,"指派資格已撤銷或尚未生效");
        h->phase=Handoff::Confirmed;++c.confirmed;L("交接 #"+std::to_string(h->id)+" 已由指派 #"+std::to_string(a->id)+" 明確確認（不代表故障已修復）");
        try_release(*h);return ActionResult::Ok;
    }
    // Host work tracked under an assignment (e.g. started inside the callback). Allowed in the callback (control).
    std::optional<WorkToken> begin_work(const AckToken& t){if(!on_owner()||out_of_scope||t.world!=id)return std::nullopt;Handoff* h=handoff(t.handoff);if(!h)return std::nullopt;
        Assignment* a=h->find(t.assignment);if(!a||t.assignment!=h->current||!a->authority)return std::nullopt;
        for(auto& j:a->jobs)if(!j){j=next_unique();++a->work;return WorkToken{id,h->id,a->id,j};}
        return std::nullopt;}   // bounded: no free tracked-work slot
    ActionResult end_work(const WorkToken& w){if(!on_owner())return ActionResult::RefusedWrongThread;if(out_of_scope)return ActionResult::RefusedOutOfScope;
        if(w.world!=id)return ActionResult::RefusedInvalid;
        Handoff* h=handoff(w.handoff);if(!h)return ActionResult::RefusedStale;Assignment* a=h->find(w.assignment);if(!a||!w.work)return ActionResult::RefusedStale;
        auto j=std::find(a->jobs.begin(),a->jobs.end(),w.work);if(j==a->jobs.end())return ActionResult::RefusedStale;   // already ended / foreign
        *j=0;--a->work;L("指派 #"+std::to_string(a->id)+" 的列管工作結束（剩 "+std::to_string(a->work)+"）");reclaim(*h);try_release(*h);return ActionResult::Ok;}

    // ---- reassignment ----
    std::optional<std::uint64_t> reassign(std::uint64_t hid,std::uint64_t candidate){
        if(!on_owner())return std::nullopt;   // refuse before touching owner-only counters
        if(mutation_gate()!=ActionResult::Ok){++c.reassign_refused;return std::nullopt;}
        auto refuse=[&](const std::string& why){++c.reassign_refused;L("改派未受理："+why);return std::optional<std::uint64_t>{};};
        if(out_of_scope)return refuse("超出安全交接範圍");
        Handoff* h=handoff(hid);if(!h)return refuse("交接不存在");
        if(h->phase==Handoff::Confirmed)return refuse("交接已確認完成");
        if(h->req.s==Reassign::Waiting)return refuse("已有未完成的改派要求（不覆寫、不串接）");
        if(!receiver_alive(candidate))return refuse("候選接收者無效");
        size_t free=0;for(auto& a:h->asg)free+=a.id==0;if(free==0)return refuse("指派紀錄已滿（舊工作尚未結束）");
        if(auto* a=h->cur())a->authority=false;   // revoke immediately on acceptance
        h->req={Reassign::Waiting,next_unique(),candidate,receivers[candidate].version};h->phase=Handoff::AwaitingTakeover;++c.reassign_accepted;
        L("改派受理：要求 #"+std::to_string(h->req.id)+"，舊指派確認資格立即撤銷；等待舊工作結束後才能生效");return h->req.id;
    }
    ActionResult cancel_reassign(std::uint64_t hid,std::uint64_t rid){if(!on_owner())return ActionResult::RefusedWrongThread;
        if(out_of_scope)return ActionResult::RefusedOutOfScope;
        Handoff* h=handoff(hid);if(!h||h->req.id!=rid)return ActionResult::RefusedInvalid;
        if(h->req.s==Reassign::Activated){++c.too_late;L("取消改派太晚：已生效，新接管者保留");return ActionResult::TooLate;}
        if(h->req.s!=Reassign::Waiting)return ActionResult::RefusedStale;
        h->req.s=Reassign::Cancelled;++c.cancelled;L("改派要求 #"+std::to_string(rid)+" 已取消：回到待接管，額度保留，舊資格不恢復");return ActionResult::Ok;}
    // Explicit owner-thread activation commit (safe point). Eligibility is necessary but not sufficient.
    ActionResult activate(std::uint64_t hid,std::uint64_t rid){
        auto g=structural_gate();if(g!=ActionResult::Ok)return g;
        if(out_of_scope)return ActionResult::RefusedOutOfScope;
        Handoff* h=handoff(hid);if(!h||h->req.id!=rid)return ActionResult::RefusedInvalid;
        if(h->req.s!=Reassign::Waiting)return h->req.s==Reassign::Activated?ActionResult::RefusedDuplicate:ActionResult::RefusedStale;
        if(!h->old_work_ended()){L("改派尚不能生效：舊指派仍有回呼或列管工作");return ActionResult::RefusedNotEligible;}
        auto it=receivers.find(h->req.candidate);
        if(it==receivers.end()||it->second.version!=h->req.candidate_version){h->req.s=Reassign::Failed;++c.reassign_failed;L("改派失敗：候選已失效或版本改變；維持待接管、保留額度");return ActionResult::Failed;}
        Assignment* slot=nullptr;for(auto& a:h->asg)if(a.id==0){slot=&a;break;}
        if(!slot)return ActionResult::RefusedNoCapacity;
        *slot={next_unique(),h->req.candidate,true,false,false,0};h->current=slot->id;h->req.s=Reassign::Activated;h->phase=Handoff::AwaitingDelivery;++c.activated;
        reclaim(*h);L("改派生效：新指派 #"+std::to_string(slot->id)+"（接收者 #"+std::to_string(h->req.candidate)+"）；交接重新送給新接管者");return ActionResult::Ok;
    }
    // Owner thread: apply posted control messages in FIFO order (first legal transition wins).
    // Out of scope: messages stay queued (not drained as if safety held).
    int process_control(){if(!on_owner()||in_host_callback||out_of_scope)return 0;int n=0;Control m;
        while(control->pop(m)){++n;++c.control_processed;
            switch(m.kind){case Control::Confirm:L(std::string("信箱：確認 → ")+name(confirm(m.ack)));break;
                case Control::Cancel:L(std::string("信箱：取消改派 → ")+name(cancel_reassign(m.handoff,m.request)));break;
                case Control::Activate:L(std::string("信箱：改派生效 → ")+name(activate(m.handoff,m.request)));break;
                case Control::EndWork:L(std::string("信箱：舊工作結束 → ")+name(end_work(m.work)));break;}}
        return n;}

    // ---- late results ----
    int settle(){
        if(mutation_gate()!=ActionResult::Ok)return 0;
        if(out_of_scope){if(ledger->published_waiting()){++c.held_out_of_scope;L("超出安全交接範圍：已發布結果保持原狀");}return 0;}
        int n=ledger->settle_all([&](const Ledger::Slot& s){
            Instance* i=find(s.instance);
            if(!i){++c.late_rejected_missing;return;}
            if(i->reservations)--i->reservations;
            if(i->state!=InstanceState::Running){++c.late_rejected_unavailable;L("晚到結果：實例 "+i->env+" 世代"+std::to_string(i->gen)+" 不可用 → 不更新 UI");return;}
            if(s.target_gen!=i->target_gen){++c.late_rejected_generation;return;}
            i->value=s.value;++c.late_applied;});
        cleanup();return n;
    }
    struct RebuildConditions{bool isolation_ok=true,resources_ok=true;};
    std::optional<std::uint64_t> rebuild(const std::string& env,RebuildConditions cond){
        auto g=structural_gate();if(g!=ActionResult::Ok)return std::nullopt;
        Instance* old=cur(env);
        auto refuse=[&](const std::string& why){++c.rebuilds_refused;L("拒絕重建 "+env+"："+why);return std::optional<std::uint64_t>{};};
        if(out_of_scope)return refuse("超出安全交接範圍");
        if(!old)return refuse("沒有實例");
        if(old->state==InstanceState::Running)return refuse("目前實例仍在執行中");
        if(!cond.isolation_ok)return refuse("隔離條件未成立");
        if(!cond.resources_ok)return refuse("資源條件未成立");
        if(!receiver_alive(active_receiver)){++c.admissions_refused;return refuse("沒有有效的主動接收途徑");}
        for(auto& s:old->services)if(!services[s].trustworthy)return refuse("共享服務 "+s+" 仍不可信");
        if(instances.size()>=instance_cap)return refuse("實例容量已滿");
        if(notice_used>=notice_cap){++c.admissions_refused;return refuse("交接容量已滿（先前交接尚未確認並安全釋放）");}
        const unsigned gen=old->gen+1;const auto svcs=old->services;
        instances.push_back({next_unique(),env,gen,InstanceState::Running,svcs});++notice_used;
        const auto nid=instances.back().id;current[env]=nid;++c.rebuilds;L("重建 "+env+" 為新實例世代"+std::to_string(gen));return nid;
    }
    ActionResult replace_service(const std::string& svc){auto g=structural_gate();if(g!=ActionResult::Ok)return g;if(out_of_scope)return ActionResult::RefusedOutOfScope;
        auto& s=services[svc];++s.gen;s.trustworthy=true;L("宿主明確替換共享服務 "+svc);return g;}
    void cleanup(){
        if(!on_owner()||depth>0||traversals>0||in_host_callback||out_of_scope)return;
        for(auto it=instances.begin();it!=instances.end();){
            if(it->state==InstanceState::Unavailable&&it->reservations==0&&it->cleanup_safe&&current[it->env]!=it->id){L("舊實例 "+it->env+" 世代"+std::to_string(it->gen)+" 退休");++c.retired;it=instances.erase(it);}
            else ++it;}
    }
private:
    // Revoked assignment records are reclaimed once their callbacks/work ended (bounded storage, no history).
    void reclaim(Handoff& h){for(auto& a:h.asg)if(a.id&&a.id!=h.current&&!a.authority&&!a.callback_active&&a.work==0)a=Assignment{};}
    void try_release(Handoff& h){
        if(h.phase!=Handoff::Confirmed||!h.callbacks_ended()||!h.release_safe||out_of_scope||in_host_callback)return;
        notice_used-=h.credits;++c.released;L("交接 #"+std::to_string(h.id)+" 的容量釋放（"+std::to_string(h.credits)+"）");h=Handoff{};}
    void deliver_one(Handoff& h){
        Assignment* a=h.cur();auto it=a?receivers.find(a->receiver):receivers.end();
        if(!a||it==receivers.end()||!a->authority){if(a){a->route_failed=true;a->authority=false;}h.phase=Handoff::AwaitingTakeover;++c.handoff_failed;
            L("交接 #"+std::to_string(h.id)+" 無法送出：接收途徑失效 → 待接管、保留額度");return;}
        const Notification note=h.note;const AckToken tok{id,h.id,a->id};const bool post_safe=it->second.post_throw_safe;Callback cb=it->second.cb;
        a->callback_active=true;in_host_callback=true;callback_handoff=h.id;++c.callbacks;
        bool threw=false;HostReply r=HostReply::KeepProcess;
        try{r=cb(*this,note,tok);}catch(...){threw=true;}
        in_host_callback=false;callback_handoff=0;
        Handoff* hp=handoff(note.handoff);Assignment* ap=hp?hp->find(tok.assignment):nullptr;
        if(ap)ap->callback_active=false;
        if(!hp)return;
        if(threw){
            // Declared post-throw safety applies before AND after acknowledgment. Unsafe -> negative out-of-scope marker
            // (no fatal handling): everything held, no further host calls, no activation/release/work draining.
            if(!post_safe){out_of_scope=true;hp->release_safe=false;L("宣告回呼失敗後狀態不安全 → 超出安全交接範圍：保留所有交接、額度與列管工作");}
            if(hp->phase==Handoff::Confirmed){hp->failed_after_ack=true;++c.failed_after_ack;
                L(std::string("確認後回呼拋例外：確認保留、不重送；宣告")+(post_safe?"安全結束，可釋放":"不安全，容量持續保留"));}
            else{if(ap){ap->route_failed=true;ap->authority=false;}hp->phase=Handoff::AwaitingTakeover;++c.handoff_failed;
                L("確認前回呼拋例外：交接未完成、保留額度，不重跑");}
        }else{++c.delivered;if(hp->phase!=Handoff::Confirmed){hp->phase=Handoff::AwaitingAck;L("回呼正常返回但未確認：交接仍待確認，額度保留");}}
        if(r==HostReply::RequestExit){process_exit_requested=true;L("宿主選擇：已請求退出（模擬，不實際退出）");}
        try_release(*hp);
    }
    bool fault(const std::string& env,const FaultSpec& spec){   // true = a new fault transition was recorded
        if(!spec.handoff_path_safe){out_of_scope=true;L("超出範圍：未宣告安全交接路徑 → 不通知、不宣稱恢復");return false;}
        std::vector<std::string> affected{env};Instance* src=cur(env);
        if(!spec.service.empty())services[spec.service].trustworthy=false;
        auto add=[&](const std::string& e){if(std::find(affected.begin(),affected.end(),e)==affected.end())affected.push_back(e);};
        if(!spec.service.empty()){for(auto& i:instances)if(current[i.env]==i.id&&std::count(i.services.begin(),i.services.end(),spec.service))add(i.env);}
        else if(!spec.dependency_known){for(auto& [e,iso]:isolated)if(!iso)add(e);}
        else if(src)for(auto& s:src->services)for(auto& i:instances)if(current[i.env]==i.id&&std::count(i.services.begin(),i.services.end(),s))add(i.env);
        // Determine NEW transitions first: a repeated report allocates nothing and cannot hit the full handoff table.
        std::vector<Instance*> fresh;for(auto& e:affected){Instance* i=cur(e);if(i&&i->state==InstanceState::Running)fresh.push_back(i);}
        if(fresh.empty()){++c.repeated_fault_attempts;return false;}
        Handoff* h=nullptr;for(auto& x:handoffs)if(!x.id){h=&x;break;}
        if(!h)throw std::logic_error("handoff table capacity invariant violated");   // each Running instance holds a credit: unreachable
        Notification note{next_unique(),spec.reason,{},{},{}};
        for(Instance* i:fresh){i->state=InstanceState::Unavailable;i->fault=spec.reason;note.envs.push_back(i->env);note.instances.push_back(i->id);note.gens.push_back(i->gen);
            L(i->env+" 世代"+std::to_string(i->gen)+" → 不可用");}
        *h=Handoff{};h->id=note.handoff;h->note=note;h->credits=note.instances.size();
        h->asg[0]={next_unique(),active_receiver,receiver_alive(active_receiver),false,!receiver_alive(active_receiver),0};h->current=h->asg[0].id;
        h->phase=receiver_alive(active_receiver)?Handoff::AwaitingDelivery:Handoff::AwaitingTakeover;
        L("建立必要故障交接 #"+std::to_string(h->id)+"（涵蓋 "+std::to_string(note.envs.size())+" 個環境）");diagnose("handoff #"+std::to_string(h->id));
        return true;
    }
};

} // namespace handoff
