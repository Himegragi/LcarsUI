#pragma once
// THROWAWAY SAFE-HANDOFF LOGIC PROTOTYPE v2 (prototypes/safe-handoff). Not framework code or an API proposal.
// Models: shared logical state becoming untrustworthy while a safe host handoff path is DECLARED available
// (fixture input, not a corruption detector); affected-scope widening; immediate unavailability across modeled
// routes; one host notification per fault transition, delivered only on the owner thread outside callbacks and
// traversals; independent environments continuing; bounded late-result disposal; explicit rebuild with new identity.
// Out of scope: actual fatal paths, process termination, memory corruption, GPU, IME, UIA.
#include <algorithm>
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
    // Any thread. Unknown, foreign, stale or duplicate tokens are rejected without allocating anything.
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
    unsigned target_gen=1; int value=0;     // one UI target inside the instance; target_gen bumps on rebuild-of-target
    size_t reservations=0;                  // accepted async work still awaiting settlement
    bool cleanup_safe=true,cleaned=false;
};
struct Service { std::string name; unsigned gen=1; bool trustworthy=true; };

// Immutable copied snapshot: names, instance ids and generations; never pointers into instances.
struct Notification { std::uint64_t seq; std::string reason; std::vector<std::string> envs; std::vector<std::uint64_t> instances; std::vector<unsigned> gens; };
enum class HostReply{KeepProcess,RequestExit};
enum class ActionResult{Ok,RefusedUnavailable,RefusedInHostCallback,RefusedNoInstance,RefusedOutOfScope,RefusedWrongThread,RefusedNotSafePoint,RefusedNoCapacity,RefusedDuplicate,Failed};
inline const char* name(ActionResult r){switch(r){case ActionResult::Ok:return "成功";case ActionResult::RefusedUnavailable:return "拒絕：不可用";
    case ActionResult::RefusedInHostCallback:return "拒絕：宿主回呼中";case ActionResult::RefusedNoInstance:return "拒絕：無實例";case ActionResult::RefusedOutOfScope:return "拒絕：超出安全交接範圍";
    case ActionResult::RefusedWrongThread:return "拒絕：非擁有者執行緒";case ActionResult::RefusedNotSafePoint:return "拒絕：不在安全點";case ActionResult::RefusedNoCapacity:return "拒絕：容量不足";case ActionResult::RefusedDuplicate:return "拒絕：名稱重複";default:return "失敗";}}

// Fixture input describing what a failing extension callback leaves behind. These are DECLARATIONS, not detection.
struct FaultSpec { bool state_still_valid=false; std::string service; bool dependency_known=true; bool handoff_path_safe=true; std::string reason="extension threw"; };
enum class DiagMode{Enabled,Disabled,Full,Throwing};

struct World {
    // Fixture capacities (exploratory): instances, notification credits, log lines, diagnostic records, async slots.
    // Notification credits: every Running instance holds one credit for its essential future fault notice. A fault
    // moves the credit into the pending notice; delivering the notice returns it. Retirement does not return it.
    // So pending.size() <= notice_used <= notice_cap, and a new instance cannot start without a credit.
    static constexpr size_t instance_cap=4,notice_cap=4,log_cap=80,diag_cap=4;
    std::thread::id owner=std::this_thread::get_id();
    std::shared_ptr<Ledger> ledger=std::make_shared<Ledger>(6);
    std::map<std::string,Service> services;
    std::map<std::string,bool> isolated;    // declared isolation of each environment (fixture input)
    std::vector<Instance> instances;
    std::map<std::string,std::uint64_t> current;   // env -> current instance id
    std::deque<Notification> pending; size_t notice_used=0;
    std::deque<std::string> log; std::vector<std::string> diag; DiagMode diag_mode=DiagMode::Enabled; bool diag_disabled_after_throw=false;
    std::function<HostReply(const Notification&)> host;
    int depth=0,traversals=0; bool in_host_callback=false;
    bool out_of_scope=false,process_exit_requested=false;  // exit is a recorded simulation only
    std::uint64_t fault_seq=0;
    // Owner-thread-only counters. Off-owner calls are refused before touching any of them.
    struct Counters{int delivered=0,handoff_failed=0,deferred_delivery=0,refused_reentry=0,refused_actions=0,repeated_fault_attempts=0,
        late_rejected_unavailable=0,late_rejected_generation=0,late_rejected_missing=0,late_applied=0,diag_failures=0,rebuilds=0,rebuilds_refused=0,retired=0,
        held_out_of_scope=0,admissions_refused=0;} c;

    bool on_owner() const{return std::this_thread::get_id()==owner;}
    void L(const std::string& s){if(log.size()>=log_cap)log.pop_front();log.push_back(s);}
    // Optional diagnostics: never affects essential status.
    void diagnose(const std::string& s){
        if(diag_mode==DiagMode::Disabled||diag_disabled_after_throw)return;
        try{
            if(diag_mode==DiagMode::Throwing)throw std::runtime_error("diagnostic sink failed");
            if(diag.size()>=diag_cap){++c.diag_failures;return;}
            diag.push_back(s);
        }catch(const std::exception&){++c.diag_failures;diag_disabled_after_throw=true;L("診斷接收器拋出例外 → 停用該接收器（不回送同一接收器）");}
    }

    Instance* find(std::uint64_t id){for(auto& i:instances)if(i.id==id)return &i;return nullptr;}
    Instance* cur(const std::string& env){auto it=current.find(env);return it==current.end()?nullptr:find(it->second);}

    // Every public mutator passes through this first: wrong thread refused WITHOUT touching state or counters;
    // inside the host callback refused (no reentry into the model from the host).
    ActionResult mutation_gate(){
        if(!on_owner())return ActionResult::RefusedWrongThread;
        if(in_host_callback){++c.refused_reentry;++c.refused_actions;return ActionResult::RefusedInHostCallback;}
        return ActionResult::Ok;
    }
    // Structural changes (rebuild, service replacement, new env) also need a safe point (fixture: simple refusal).
    ActionResult structural_gate(){auto g=mutation_gate();if(g!=ActionResult::Ok)return g;
        if(depth>0||traversals>0){++c.refused_actions;return ActionResult::RefusedNotSafePoint;}return g;}

    ActionResult add_env(const std::string& env,std::vector<std::string> svc,bool iso){
        auto g=structural_gate();if(g!=ActionResult::Ok)return g;
        if(out_of_scope)return ActionResult::RefusedOutOfScope;
        if(current.count(env)||isolated.count(env)){++c.refused_actions;L("拒絕新增 "+env+"：名稱已存在（不覆寫既有實例）");return ActionResult::RefusedDuplicate;}
        if(instances.size()>=instance_cap||notice_used>=notice_cap){++c.admissions_refused;L("拒絕新增 "+env+"：實例或通知容量不足");return ActionResult::RefusedNoCapacity;}
        for(auto& s:svc)services.try_emplace(s,Service{s});
        isolated[env]=iso;instances.push_back({next_unique(),env,1,InstanceState::Running,svc});current[env]=instances.back().id;++notice_used;L("建立 "+env+" 世代1");return g;}

    // ---- ordinary routes: all refused immediately when the current instance is unavailable ----
    ActionResult gate(const std::string& env){
        auto g=mutation_gate();if(g!=ActionResult::Ok)return g;
        if(out_of_scope){++c.refused_actions;return ActionResult::RefusedOutOfScope;}
        Instance* i=cur(env);if(!i){++c.refused_actions;return ActionResult::RefusedNoInstance;}
        if(i->state!=InstanceState::Running){++c.refused_actions;return ActionResult::RefusedUnavailable;}
        return ActionResult::Ok;
    }
    ActionResult set_value(const std::string& env,int v){auto g=gate(env);if(g!=ActionResult::Ok)return g;cur(env)->value=v;return g;}
    ActionResult start_op(const std::string& env,Token& t){auto g=gate(env);if(g!=ActionResult::Ok)return g;Instance* i=cur(env);
        if(!ledger->reserve(i->id,i->target_gen,t))return ActionResult::RefusedNoCapacity;++i->reservations;L(env+" 啟動非同步工作（預留結果槽位"+std::to_string(t.slot)+"）");return g;}
    ActionResult rebuild_target(const std::string& env){auto g=gate(env);if(g!=ActionResult::Ok)return g;Instance* i=cur(env);++i->target_gen;i->value=0;L(env+" 目標重建為世代"+std::to_string(i->target_gen));return g;}

    // ---- extension callback boundary ----
    ActionResult dispatch(const std::string& env,const std::function<void()>& cb,const FaultSpec& spec){
        auto g=gate(env);
        if(g==ActionResult::RefusedInHostCallback){L("宿主回呼中的派送重入被拒絕");return g;}
        if(g==ActionResult::RefusedUnavailable){++c.repeated_fault_attempts;return g;}  // failed generation: no notification storm
        if(g!=ActionResult::Ok)return g;
        ++depth;bool threw=false;std::string what;
        try{cb();}catch(const std::exception& e){threw=true;what=e.what();}catch(...){threw=true;what="unknown exception";}
        --depth;
        if(!threw)return ActionResult::Ok;
        diagnose(env+" 回呼例外："+what);
        if(spec.state_still_valid){L(env+" 回呼失敗，但宣告必要狀態仍有效 → 只回報失敗");return ActionResult::Failed;}
        fault(env,spec);return ActionResult::Failed;
    }
    // Shared service declared untrustworthy (e.g. by its owner).
    ActionResult service_fault(const std::string& svc,bool handoff_safe=true){
        auto g=mutation_gate();if(g!=ActionResult::Ok)return g;
        if(out_of_scope)return ActionResult::RefusedOutOfScope;
        FaultSpec s;s.service=svc;s.handoff_path_safe=handoff_safe;s.reason="shared service "+svc+" untrustworthy";
        std::string first;for(auto& i:instances)if(current[i.env]==i.id&&std::count(i.services.begin(),i.services.end(),svc)){first=i.env;break;}
        if(first.empty())return ActionResult::RefusedNoInstance;
        fault(first,s);return g;}

    // ---- host handoff: owner thread, outside callbacks and traversals ----
    int deliver(){
        if(!on_owner())return 0;
        // Safe handoff path no longer declared: hold every pending notice and its credits; no host callback.
        if(out_of_scope){if(!pending.empty())L("超出安全交接範圍：保留 "+std::to_string(pending.size())+" 則待送通知與額度，不呼叫宿主");return 0;}
        if(depth>0||traversals>0||in_host_callback){if(!pending.empty())++c.deferred_delivery;return 0;}
        int n=0;
        while(!pending.empty()){
            Notification note=pending.front();pending.pop_front();  // stable copy, no borrowed instance pointers
            // Fixture policy: an attempted handoff (delivered, threw, or no receiver) returns the credit; no auto retry.
            notice_used-=note.instances.size();
            if(!host){++c.handoff_failed;++n;L("沒有登記宿主接收者 → 交接失敗已記錄；不算已遞送、不默認保留程序、不重試");continue;}
            in_host_callback=true;
            try{
                HostReply r=host(note);++c.delivered;
                if(r==HostReply::RequestExit){process_exit_requested=true;L("宿主選擇：已請求退出（模擬，只記錄，不實際退出）");}
                else L("宿主選擇：保留程序；受影響實例仍不可用");
            }catch(...){++c.handoff_failed;L("宿主通知回呼拋出例外 → 交接失敗已記錄；不重試、不視為已確認");}
            in_host_callback=false;++n;
        }
        cleanup();return n;
    }
    // ---- late results: owner thread settlement ----
    int settle(){
        if(mutation_gate()!=ActionResult::Ok)return 0;
        if(out_of_scope){if(ledger->published_waiting()){++c.held_out_of_scope;L("超出安全交接範圍：已發布結果與預留保持原狀，不更新 UI、不釋放（未解決，不宣稱已收尾）");}return 0;}
        int n=ledger->settle_all([&](const Ledger::Slot& s){
            Instance* i=find(s.instance);
            if(!i){++c.late_rejected_missing;L("晚到結果：實例已退休，只歸還槽位");return;}
            if(i->reservations)--i->reservations;
            if(i->state!=InstanceState::Running){++c.late_rejected_unavailable;L("晚到結果：實例 "+i->env+" 世代"+std::to_string(i->gen)+" 不可用 → 不更新 UI，只結清收尾");return;}
            if(s.target_gen!=i->target_gen){++c.late_rejected_generation;L("晚到結果：目標世代不符（實例仍執行中）→ 不更新");return;}
            i->value=s.value;++c.late_applied;L("結果套用到 "+i->env);
        });
        cleanup();return n;
    }
    // ---- explicit rebuild: new identity; refused unless declared conditions and capacity hold ----
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
        for(auto& s:old->services)if(!services[s].trustworthy)return refuse("共享服務 "+s+" 仍不可信（不會自動修復）");
        if(instances.size()>=instance_cap)return refuse("實例容量已滿（舊實例收尾未完成）");
        if(notice_used>=notice_cap){++c.admissions_refused;return refuse("通知容量已滿（先前的故障通知尚未遞送）");}
        const unsigned gen=old->gen+1;const auto svcs=old->services;
        instances.push_back({next_unique(),env,gen,InstanceState::Running,svcs});++notice_used;
        const auto id=instances.back().id;current[env]=id;++c.rebuilds;L("重建 "+env+" 為新實例世代"+std::to_string(gen)+"（舊實例保持不可用）");return id;
    }
    // Host explicitly replaces a service (new generation); never silent.
    ActionResult replace_service(const std::string& svc){auto g=structural_gate();if(g!=ActionResult::Ok)return g;
        if(out_of_scope)return ActionResult::RefusedOutOfScope;
        auto& s=services[svc];++s.gen;s.trustworthy=true;L("宿主明確替換共享服務 "+svc+" → 世代"+std::to_string(s.gen));return g;}
    // Retire unavailable instances once their accepted work has settled and cleanup is declared safe (safe point only).
    void cleanup(){
        if(!on_owner()||depth>0||traversals>0||in_host_callback||out_of_scope)return;  // out of scope: no retirement
        for(auto it=instances.begin();it!=instances.end();){
            if(it->state==InstanceState::Unavailable&&it->reservations==0&&it->cleanup_safe&&current[it->env]!=it->id){
                L("舊實例 "+it->env+" 世代"+std::to_string(it->gen)+" 收尾完成並退休");++c.retired;it=instances.erase(it);}
            else ++it;
        }
        for(auto& i:instances)if(i.state==InstanceState::Unavailable&&i.reservations==0&&i.cleanup_safe)i.cleaned=true;
    }
private:
    void fault(const std::string& env,const FaultSpec& spec){
        if(!spec.handoff_path_safe){
            out_of_scope=true;L("超出範圍：未宣告安全交接路徑 → 不通知、不宣稱恢復（致命路徑不在本原型）");return;}
        std::vector<std::string> affected{env};
        Instance* src=cur(env);
        if(!spec.service.empty())services[spec.service].trustworthy=false;  // no silent repair later
        auto add=[&](const std::string& e){if(std::find(affected.begin(),affected.end(),e)==affected.end())affected.push_back(e);};
        if(!spec.service.empty()){for(auto& i:instances)if(current[i.env]==i.id&&std::count(i.services.begin(),i.services.end(),spec.service))add(i.env);}
        else if(!spec.dependency_known){for(auto& [e,iso]:isolated)if(!iso)add(e);}   // unknown dependency: widen
        else if(src)for(auto& s:src->services)for(auto& i:instances)if(current[i.env]==i.id&&std::count(i.services.begin(),i.services.end(),s))add(i.env);
        Notification note{++fault_seq,spec.reason,{},{},{}};
        for(auto& e:affected){Instance* i=cur(e);if(!i||i->state!=InstanceState::Running)continue;
            i->state=InstanceState::Unavailable;i->fault_seq=note.seq;i->fault=spec.reason;note.envs.push_back(e);note.instances.push_back(i->id);note.gens.push_back(i->gen);
            L(e+" 世代"+std::to_string(i->gen)+" → 不可用（立即拒絕普通操作）");}
        if(note.envs.empty())return;
        // Credits of these instances move into the notice (notice_used unchanged), so pending can never exceed notice_cap.
        pending.push_back(note);L("排入一則宿主通知 #"+std::to_string(note.seq)+"（涵蓋 "+std::to_string(note.envs.size())+" 個環境）");
        diagnose("fault #"+std::to_string(note.seq));
    }
};

} // namespace handoff
