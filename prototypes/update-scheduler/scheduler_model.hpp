#pragma once
// THROWAWAY LOGIC PROTOTYPE v5 (prototypes/update-scheduler). Not a framework scheduler or API proposal.
// Models: fixed notification rounds, mid-round yield with hybrid (time + count) budget, safe structural commit
// between rounds, pending removal, BOUNDED ordinary capacity, terminal-report reservation before an operation
// starts (fixed slot ledger), finite completion priority with a persisted streak, and non-blocking scope close.
// Everything runs on the owner thread except TerminalLedger::publish / cancel_requested (thread-safe).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sched {

struct Clock { virtual ~Clock()=default; virtual double ms()=0; };
struct SimClock final:Clock { double t=0; double ms() override{return t;} void advance(double d){t+=d;} };
struct SteadyClock final:Clock {
    std::chrono::steady_clock::time_point origin=std::chrono::steady_clock::now();
    double ms() override{return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-origin).count();}
};
struct Trace {
    std::deque<std::string> lines; size_t limit;
    explicit Trace(size_t l=100000):limit(l){}
    void add(std::string s){if(lines.size()>=limit)lines.pop_front();lines.push_back(std::move(s));}
};
struct Service { int leases=0; };  // shared service borrowed by environments: lease counter only (fixture)

// ---- terminal-report ledger: the single authority for reserved operations, their leases and results ----
// Fixed slots allocated once. A token is (ledger, slot, generation); the generation advances when a slot is released,
// so old tokens are rejected. Publication stores a fixed small result in the slot and enqueues the slot index in
// a fixed ring of the same size; a slot can be published at most once while Reserved, so the ring never overflows.
struct Result { int value; bool cancel_seen; };
// Experimental identity, not a security or ABI design: a token carries the id of the ledger that issued it.
// Ledger ids come from a process-wide counter and are never reused, so a token cannot match another or a newer ledger.
struct Token { std::uint64_t ledger=0; std::uint32_t slot=0, gen=0; bool valid() const{return gen!=0;} };
inline std::uint64_t next_ledger_id(){static std::atomic<std::uint64_t> n{0};return ++n;}
struct TerminalLedger {
    enum class SlotState{Free,Reserved,Published};
    struct Slot{std::uint32_t gen=1;SlotState s=SlotState::Free;Result r{};int target=0;unsigned target_gen=0;bool cancel_requested=false;};
    mutable std::mutex m;
    const std::uint64_t id=next_ledger_id();
    std::vector<Slot> slots; std::vector<std::uint32_t> ring; size_t head=0,count=0;
    int published=0,rejected_publish=0,released=0;
    explicit TerminalLedger(size_t cap):slots(cap),ring(cap){}
    size_t cap() const{return slots.size();}
    // owner thread
    bool reserve(int target,unsigned target_gen,Token& out){std::lock_guard l(m);
        for(std::uint32_t i=0;i<slots.size();++i)if(slots[i].s==SlotState::Free){auto& s=slots[i];s.s=SlotState::Reserved;s.target=target;s.target_gen=target_gen;s.cancel_requested=false;out={id,i,s.gen};return true;}
        return false;}
    // any thread: rejects unknown / stale / duplicate tokens without allocating anything
    bool publish(Token t,Result r){std::lock_guard l(m);
        if(t.ledger!=id||t.slot>=slots.size()||slots[t.slot].gen!=t.gen||slots[t.slot].s!=SlotState::Reserved){++rejected_publish;return false;}
        slots[t.slot].s=SlotState::Published;slots[t.slot].r=r;ring[(head+count)%ring.size()]=t.slot;++count;++published;return true;}
    bool cancel_requested(Token t) const{std::lock_guard l(m);return t.ledger==id&&t.slot<slots.size()&&slots[t.slot].gen==t.gen&&slots[t.slot].cancel_requested;}
    // owner thread
    bool take(std::uint32_t& idx,Slot& copy){std::lock_guard l(m);if(!count)return false;idx=ring[head];head=(head+1)%ring.size();--count;copy=slots[idx];return true;}
    void release(std::uint32_t idx){std::lock_guard l(m);slots[idx].s=SlotState::Free;++slots[idx].gen;++released;}
    void request_cancel_all(){std::lock_guard l(m);for(auto& s:slots)if(s.s==SlotState::Reserved)s.cancel_requested=true;}
    size_t ready() const{std::lock_guard l(m);return count;}
    size_t used() const{std::lock_guard l(m);size_t n=0;for(auto& s:slots)n+=s.s!=SlotState::Free;return n;}
    bool first_reserved(Token& t) const{std::lock_guard l(m);for(std::uint32_t i=0;i<slots.size();++i)if(slots[i].s==SlotState::Reserved){t={id,i,slots[i].gen};return true;}return false;}
};

struct Budget { double ms; int count; };
enum class Stop { Idle, Count, Time, Deferred, CommitHeld };
inline const char* stop_name(Stop s){switch(s){case Stop::Idle:return "無工作";case Stop::Count:return "工作數額度";case Stop::Time:return "時間額度";
    case Stop::Deferred:return "重入延後";default:return "提交等待外層遍歷";}}
struct SliceResult { Stop stop; int executed; double elapsed; };
struct Work { enum Kind{Notice,Event} kind; char key; int id; int target; unsigned gen; };

struct Env {
    enum class TargetState{Live,PendingRemoval,Removed};
    struct Target{unsigned gen=1;TargetState s=TargetState::Live;int value=0;};
    enum class ScopeState{Running,Closing,Closed};

    int id; std::string name; Clock& clock; Trace& trace; Service& service;
    // Fixture capacities (exploratory, not defaults): ordinary entries and terminal slots.
    size_t ordinary_cap; std::shared_ptr<TerminalLedger> ledger;
    int cleanup_quota=2,cleanup_streak=0;   // finite completion priority; streak persists across slices
    std::map<char,int> value;
    std::map<int,Target> targets;
    std::vector<Work> round; size_t cursor=0; unsigned round_no=0;
    std::vector<Work> next;
    std::vector<int> pending_removals;      // teardown metadata: at most one per target, not ordinary capacity
    int depth=0,max_depth=0,outer_traversals=0;
    bool in_slice=false,wake_pending=false;int wake_requests=0;
    std::map<char,std::function<void(Env&)>> on_notice;
    std::function<void(Env&,int target)> on_event;
    ScopeState scope=ScopeState::Running; double close_deadline=0; bool overdue=false;
    bool service_lease_held=false;
    int event_seq=0; bool verbose=true;
    size_t peak_round=0,peak_next=0,peak_used=0; int ordinary_gap=0,max_ordinary_gap=0;
    // Physical backing store: round keeps its consumed prefix until promotion, so retained Work records can
    // approach 2*cap (round + next), separate from the logical used count.
    size_t peak_retained=0,peak_capacity=0;
    size_t pending_ev=0;                     // accepted events not yet dispatched (incremental, O(1))
    int delivery_gap=0,max_delivery_gap=0;   // units between actual event deliveries while events are pending
    struct Counters{int offers=0,accepted=0,not_accepted=0,events_accepted=0,events_executed=0,events_rejected_delivery=0,events_discarded=0,
        actions_rejected=0,ops_started=0,ops_not_started=0,notices_enqueued=0,notices_coalesced=0,notices_dispatched=0,notices_suppressed=0,
        commits=0,promotions=0,completions_settled=0,completions_rejected_stale_target=0,ordinary_units=0,cleanup_units=0;} c;

    Env(int i,std::string n,Clock& cl,Trace& t,Service& s,size_t ordinary=64,size_t terminal=8)
        :id(i),name(std::move(n)),clock(cl),trace(t),service(s),ordinary_cap(ordinary),ledger(std::make_shared<TerminalLedger>(terminal)){
        round.reserve(ordinary);next.reserve(ordinary);}
    template<class... A> void log(A&&... a){if(!verbose)return;std::ostringstream o;o.setf(std::ios::fixed);o.precision(3);
        o<<"[t="<<clock.ms()<<"ms "<<name<<" 輪"<<round_no<<" 游標"<<cursor<<"/"<<round.size()<<"] ";(o<<...<<a);trace.add(o.str());}

    // Ordinary capacity counts every accepted, not-yet-disposed entry: current-round remainder + next round
    // + the entry currently executing (it was popped from the remainder but is not yet disposed).
    size_t ordinary_used() const{return (round.size()-cursor)+next.size()+(depth>0?1:0);}
    size_t ordinary_free() const{return ordinary_used()>=ordinary_cap?0:ordinary_cap-ordinary_used();}
    size_t pending_events() const{return pending_ev;}

    // ---- ordinary public entry points ----
    bool admit(const char* what){if(scope==ScopeState::Running)return true;++c.actions_rejected;log("拒絕普通操作（範圍非執行中）：",what);return false;}
    // Direct value: if a next-round notice for k already exists, coalesce (no new slot). Otherwise a slot is
    // needed; when full the set is NOT ACCEPTED before the value changes, so no notice is ever lost.
    bool set(char k,int v){
        if(!admit("設定值"))return false;
        ++c.offers;const bool coalesce=has_next_notice(k);
        if(!coalesce&&ordinary_free()==0){++c.not_accepted;log("設定 ",k,"=",v," 未受理：普通容量已滿（值未改變）");return false;}
        ++c.accepted;value[k]=v;log(k,"=",v,"（立即可讀）");
        if(coalesce){++c.notices_coalesced;log(k," 的下一輪通知已存在，合併");}
        else{next.push_back({Work::Notice,k,0,0,0});++c.notices_enqueued;track();}
        return true;
    }
    bool add_target(int t){if(!admit("建立目標"))return false;if(targets.count(t)){++c.actions_rejected;return false;}targets[t];log("建立目標 ",t," 世代1");return true;}
    bool post_event(int t){
        if(!admit("送事件"))return false;
        auto it=targets.find(t);
        if(it==targets.end()||it->second.s!=TargetState::Live){++c.actions_rejected;log("拒絕新操作：目標",t,"不存在或待移除");return false;}
        ++c.offers;
        if(ordinary_free()==0){++c.not_accepted;log("事件未受理：普通容量已滿（",ordinary_used(),"/",ordinary_cap,"）");return false;}
        ++c.accepted;next.push_back({Work::Event,0,++event_seq,t,it->second.gen});++c.events_accepted;++pending_ev;track();log("受理事件#",event_seq,"→ 目標",t);return true;
    }
    // Removal / invalidation use teardown metadata, not ordinary capacity: a full queue cannot block them.
    bool request_remove(int t){
        if(!admit("移除"))return false;
        auto it=targets.find(t);if(it==targets.end()||it->second.s!=TargetState::Live){++c.actions_rejected;return false;}
        it->second.s=TargetState::PendingRemoval;pending_removals.push_back(t);log("移除目標 ",t," 已受理 → 待移除");return true;
    }
    bool reuse_target(int t){if(!admit("重建目標"))return false;
        auto it=targets.find(t);if(it==targets.end()||it->second.s!=TargetState::Removed){++c.actions_rejected;return false;}
        ++it->second.gen;it->second.s=TargetState::Live;it->second.value=0;log("目標",t,"以新世代",it->second.gen,"重新建立");return true;}
    // An operation starts only if a terminal slot can be reserved first; the service lease belongs to the slot.
    Token start_op(int target){
        Token t{};
        if(!admit("啟動操作"))return t;
        auto it=targets.find(target);
        if(it==targets.end()||it->second.s!=TargetState::Live){++c.actions_rejected;log("拒絕啟動操作：目標",target,"不存在或非有效");return t;}
        if(!ledger->reserve(target,it->second.gen,t)){++c.ops_not_started;log("未啟動操作：終態槽位已滿（",ledger->used(),"/",ledger->cap(),"），不取得租用");return Token{};}
        ++service.leases;++c.ops_started;log("操作啟動：預留終態槽位",t.slot,"世代",t.gen,"，租用共用服務 → 租用數",service.leases);return t;
    }
    size_t outstanding() const{return ledger->used();}
    void close(double diagnostic_ms){
        if(scope!=ScopeState::Running)return;
        scope=ScopeState::Closing;close_deadline=clock.ms()+diagnostic_ms;log("關閉要求：收尾中，診斷期限 t=",close_deadline,"ms");
        for(auto& [t,tg]:targets)if(tg.s==TargetState::Live){tg.s=TargetState::PendingRemoval;pending_removals.push_back(t);}
        ledger->request_cancel_all();log("對所有已預留操作送出取消請求（請求≠成功）");
    }
    void tick(){
        if(scope!=ScopeState::Closing)return;
        if(outstanding()>0){if(!overdue&&clock.ms()>=close_deadline){overdue=true;log("收尾逾期：仍有",outstanding(),"個終態槽位未結（仍為收尾中）");}return;}
        if(depth!=0||in_slice||outer_traversals!=0)return;
        if(cursor==round.size()&&next.empty()&&pending_removals.empty()){
            if(service_lease_held){--service.leases;service_lease_held=false;}
            scope=ScopeState::Closed;log("所有終態槽位與租用已結算，範圍已關閉");}
    }
    bool has_work() const{return cursor<round.size()||!next.empty()||ledger->ready()>0||wake_pending||!pending_removals.empty();}

    SliceResult slice(Budget b){
        if(!(b.count>=1)||!(b.ms>0))throw std::invalid_argument("budget must have count>=1 and ms>0");
        if(in_slice){++wake_requests;if(!wake_pending){wake_pending=true;log("重入要求 → 記錄一次待喚醒（不遞迴）");}return {Stop::Deferred,0,0};}
        in_slice=true;wake_pending=false;const double start=clock.ms();int n=0;Stop stop=Stop::Idle;
        for(;;){
            if(!has_work()){stop=Stop::Idle;break;}
            if(n>=b.count){stop=Stop::Count;break;}
            if(clock.ms()-start>=b.ms){stop=Stop::Time;break;}
            const bool guard=depth!=0||outer_traversals!=0;
            const bool ordinary_possible=cursor<round.size()||(!guard&&(!pending_removals.empty()||!next.empty()));
            if(ledger->ready()>0&&(cleanup_streak<cleanup_quota||!ordinary_possible)){
                settle_one();++cleanup_streak;++c.cleanup_units;++n;if(ordinary_possible)max_ordinary_gap=std::max(max_ordinary_gap,++ordinary_gap);
                delivery(false);continue;}
            if(cursor<round.size()){Work w=round[cursor++];dispatch(w);ordinary_unit();++n;delivery(w.kind==Work::Event);continue;}
            if(guard){stop=Stop::CommitHeld;log("本輪已完成，但外層遍歷仍在 → 不提交、不開始下一輪");break;}
            if(!pending_removals.empty()){commit();ordinary_unit();++n;delivery(false);continue;}
            if(next.empty()){stop=Stop::Idle;break;}
            round.swap(next);next.clear();cursor=0;++round_no;++c.promotions;ordinary_unit();++n;track();delivery(false);log("新一輪開始，成員固定 ",round.size()," 項");
        }
        const double elapsed=clock.ms()-start;in_slice=false;
        if(stop==Stop::Count||stop==Stop::Time)log("讓出（",stop_name(stop),"）：",n,"項，普通佔用",ordinary_used(),"/",ordinary_cap,"，終態佔用",outstanding(),"/",ledger->cap());
        return {stop,n,elapsed};
    }
private:
    bool has_next_notice(char k) const{for(auto& w:next)if(w.kind==Work::Notice&&w.key==k)return true;return false;}
    void track(){peak_round=std::max(peak_round,round.size());peak_next=std::max(peak_next,next.size());peak_used=std::max(peak_used,ordinary_used());
        peak_retained=std::max(peak_retained,round.size()+next.size());peak_capacity=std::max(peak_capacity,round.capacity()+next.capacity());}
    void delivery(bool event_delivered){if(event_delivered)delivery_gap=0;else if(pending_ev>0)max_delivery_gap=std::max(max_delivery_gap,++delivery_gap);}
    void ordinary_unit(){cleanup_streak=0;ordinary_gap=0;++c.ordinary_units;}
    void settle_one(){
        std::uint32_t idx;TerminalLedger::Slot s;if(!ledger->take(idx,s))return;
        ++c.completions_settled;--service.leases;
        auto t=targets.find(s.target);
        if(scope==ScopeState::Running&&t!=targets.end()&&t->second.s==TargetState::Live&&t->second.gen==s.target_gen){t->second.value=s.r.value;log("終態結算槽位",idx,"：結果套用到目標",s.target);}
        else{++c.completions_rejected_stale_target;log("終態結算槽位",idx,"：拒絕回寫（目標失效、世代不符或收尾中）");}
        ledger->release(idx);log("槽位",idx,"歸還（世代前進），租用數",service.leases);
    }
    void dispatch(const Work& w){
        ++depth;max_depth=std::max(max_depth,depth);
        if(w.kind==Work::Notice){
            if(scope!=ScopeState::Running){++c.notices_suppressed;log("通知 ",w.key," 不遞送：範圍收尾中，訂閱已解除（抑制）");}
            else{++c.notices_dispatched;log("派送通知 ",w.key,"，讀值=",value[w.key]);if(auto f=on_notice.find(w.key);f!=on_notice.end())f->second(*this);}
        }else{
            --pending_ev;
            auto t=targets.find(w.target);
            if(t!=targets.end()&&t->second.s==TargetState::Live&&t->second.gen==w.gen){++c.events_executed;log("執行事件#",w.id,"→ 目標",w.target);if(on_event)on_event(*this,w.target);}
            else{++c.events_rejected_delivery;log("拒絕遞送事件#",w.id,"：目標",w.target,"失效（已記錄，未丟棄）");}
        }
        --depth;
    }
    void commit(){for(int t:pending_removals){targets[t].s=TargetState::Removed;log("提交：實體移除目標 ",t);}pending_removals.clear();++c.commits;}
};

// Host: round-robin over runnable environments, one slice per step.
struct Host {
    std::vector<Env*> envs; size_t rr=0;
    Env* step(Budget b,SliceResult* out=nullptr){
        for(auto* e:envs)e->tick();
        for(size_t k=0;k<envs.size();++k){Env* e=envs[(rr+k)%envs.size()];
            if(e->has_work()){rr=(rr+k+1)%envs.size();auto r=e->slice(b);if(out)*out=r;e->tick();return e;}}
        return nullptr;
    }
    bool idle(){for(auto* e:envs)if(e->has_work())return false;return true;}
};

} // namespace sched
