#pragma once
// THROWAWAY LOGIC PROTOTYPE (prototypes/update-scheduler). Not a framework scheduler or API proposal.
// Models: fixed notification rounds, mid-round yield with hybrid (time + count) budget, safe structural commit
// between rounds, pending removal, mailbox-delivered async completions, and non-blocking scope close.
// Everything runs on the owner thread except Mailbox::post (thread-safe).
#include <algorithm>
#include <chrono>
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

// Only owner-safe data crosses threads: environment id, operation id and a value. No UI pointers.
struct Completion { int env; int op; int value; bool cancel_seen; };
struct Mailbox {
    std::mutex m; std::deque<Completion> q;
    void post(Completion c){std::lock_guard l(m);q.push_back(c);}
    // Bounded take: at most `limit` messages per call; the rest stay queued for later steps.
    std::vector<Completion> take(size_t limit){std::lock_guard l(m);std::vector<Completion> out;
        while(!q.empty()&&out.size()<limit){out.push_back(q.front());q.pop_front();}return out;}
    size_t size(){std::lock_guard l(m);return q.size();}
};
struct Service { int leases=0; };  // shared service borrowed by environments: lease counter only (fixture)

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
    enum class OpState{Pending,CancelRequested,Settled};
    struct Op{int id;OpState s;int target;unsigned gen;bool lease;};

    int id; std::string name; Clock& clock; Trace& trace; Service& service;
    std::map<char,int> value;
    std::map<int,Target> targets;
    std::vector<Work> round; size_t cursor=0; unsigned round_no=0;
    std::vector<Work> next;
    std::vector<int> pending_removals;
    std::deque<Completion> inbox;           // admitted completions, processed as charged work inside slices
    int depth=0,max_depth=0;                // dispatch on stack
    int outer_traversals=0;                 // fixture: an outer traversal still uses the old structure
    bool in_slice=false,wake_pending=false;int wake_requests=0;  // dispatch-on-stack guard vs resumable round
    std::map<char,std::function<void(Env&)>> on_notice;
    std::function<void(Env&,int target)> on_event;
    ScopeState scope=ScopeState::Running; double close_deadline=0; bool overdue=false; std::vector<Op> ops; int next_op=1;
    bool service_lease_held=false;
    int event_seq=0;
    bool verbose=true;  // benchmark turns trace formatting off inside timed slices
    struct Counters{int events_accepted=0,events_executed=0,events_rejected_delivery=0,events_discarded=0,
        actions_rejected=0,notices_enqueued=0,notices_coalesced=0,notices_dispatched=0,notices_suppressed=0,commits=0,promotions=0,
        completions_admitted=0,completions_rejected_stale_target=0,completions_unknown=0;} c;

    Env(int i,std::string n,Clock& cl,Trace& t,Service& s):id(i),name(std::move(n)),clock(cl),trace(t),service(s){}
    template<class... A> void log(A&&... a){if(!verbose)return;std::ostringstream o;o.setf(std::ios::fixed);o.precision(3);
        o<<"[t="<<clock.ms()<<"ms "<<name<<" 輪"<<round_no<<" 游標"<<cursor<<"/"<<round.size()<<"] ";(o<<...<<a);trace.add(o.str());}

    // ---- ordinary public entry points: all rejected without mutation unless Running ----
    bool admit(const char* what){if(scope==ScopeState::Running)return true;++c.actions_rejected;log("拒絕普通操作（範圍非執行中）：",what);return false;}
    // Successful direct value set: stored immediately; notice goes to the NEXT round (never dispatched recursively).
    bool set(char k,int v){if(!admit("設定值"))return false;value[k]=v;log(k,"=",v,"（立即可讀）");enqueue_notice(k);return true;}
    bool add_target(int t){if(!admit("建立目標"))return false;if(targets.count(t)){++c.actions_rejected;return false;}targets[t];log("建立目標 ",t," 世代1");return true;}
    bool post_event(int t){
        if(!admit("送事件"))return false;
        auto it=targets.find(t);
        if(it==targets.end()||it->second.s!=TargetState::Live){++c.actions_rejected;log("拒絕新操作：目標",t,"不存在或待移除");return false;}
        next.push_back({Work::Event,0,++event_seq,t,it->second.gen});++c.events_accepted;log("受理事件#",event_seq,"→ 目標",t,"（排入下一輪）");return true;
    }
    bool request_remove(int t){
        if(!admit("移除"))return false;
        auto it=targets.find(t);if(it==targets.end()||it->second.s!=TargetState::Live){++c.actions_rejected;return false;}
        it->second.s=TargetState::PendingRemoval;pending_removals.push_back(t);log("移除目標 ",t," 已受理 → 待移除（實體保留到提交點）");return true;
    }
    // Physically removed id reused: new generation, so older queued work/results cannot reach it.
    bool reuse_target(int t){if(!admit("重建目標"))return false;
        auto it=targets.find(t);if(it==targets.end()||it->second.s!=TargetState::Removed){++c.actions_rejected;return false;}
        ++it->second.gen;it->second.s=TargetState::Live;it->second.value=0;log("目標",t,"以新世代",it->second.gen,"重新建立");return true;}
    int start_op(int target){
        if(!admit("啟動操作"))return 0;
        auto it=targets.find(target);
        if(it==targets.end()||it->second.s!=TargetState::Live){++c.actions_rejected;log("拒絕啟動操作：目標",target,"不存在或非有效（不取得租用）");return 0;}
        ops.push_back({next_op,OpState::Pending,target,it->second.gen,true});++service.leases;
        log("操作#",next_op,"啟動（屬範圍，不屬視圖），租用共用服務 → 租用數",service.leases);return next_op++;
    }
    int outstanding() const{int n=0;for(auto& o:ops)n+=o.s!=OpState::Settled;return n;}
    void close(double diagnostic_ms){
        if(scope!=ScopeState::Running)return;
        scope=ScopeState::Closing;close_deadline=clock.ms()+diagnostic_ms;log("關閉要求：收尾中，診斷期限 t=",close_deadline,"ms");
        for(auto& [t,tg]:targets)if(tg.s==TargetState::Live){tg.s=TargetState::PendingRemoval;pending_removals.push_back(t);}
        for(auto& o:ops)if(o.s==OpState::Pending){o.s=OpState::CancelRequested;log("對操作#",o.id,"送出取消請求（請求≠成功）");}
    }
    // Internal completion lane (not an ordinary entry point): admitted while Closing.
    void admit_completion(const Completion& m){inbox.push_back(m);}
    void tick(){
        if(scope!=ScopeState::Closing)return;
        if(outstanding()>0){if(!overdue&&clock.ms()>=close_deadline){overdue=true;log("收尾逾期：仍有",outstanding(),"項未結（狀態仍為收尾中，非已關閉）");}return;}
        // Same active-use guard as commit: no dispatch on the stack, no slice running, no outer traversal.
        if(depth!=0||in_slice||outer_traversals!=0)return;
        if(cursor==round.size()&&next.empty()&&inbox.empty()&&pending_removals.empty()){
            if(service_lease_held){--service.leases;service_lease_held=false;}
            scope=ScopeState::Closed;log("所有操作與租用已結算，範圍已關閉");}
    }
    size_t pending_work() const{return (round.size()-cursor)+next.size()+inbox.size();}
    bool has_work() const{return pending_work()>0||wake_pending||!pending_removals.empty();}

    // ---- one budgeted slice. Every unit (completion, notice/event, commit, round promotion) is charged as one
    // item and is started only after the boundary check; a started unit is never interrupted. ----
    SliceResult slice(Budget b){
        if(!(b.count>=1)||!(b.ms>0))throw std::invalid_argument("budget must have count>=1 and ms>0");
        if(in_slice){++wake_requests;if(!wake_pending){wake_pending=true;log("重入要求：派送進行中 → 記錄一次待喚醒（不遞迴）");}
            else log("重入要求：已有待喚醒，合併");return {Stop::Deferred,0,0};}
        in_slice=true;wake_pending=false;const double start=clock.ms();int n=0;Stop stop=Stop::Idle;
        for(;;){
            if(!has_work()){stop=Stop::Idle;break;}
            if(n>=b.count){stop=Stop::Count;break;}
            if(clock.ms()-start>=b.ms){stop=Stop::Time;break;}
            if(!inbox.empty()){Completion m=inbox.front();inbox.pop_front();complete(m);++n;continue;}
            if(cursor<round.size()){Work w=round[cursor++];dispatch(w);++n;continue;}
            // Round complete. Commit (and then promotion) only with no dispatch or outer traversal on the stack.
            if(depth!=0||outer_traversals!=0){stop=Stop::CommitHeld;log("本輪已完成，但外層遍歷仍在 → 不提交、不開始下一輪");break;}
            if(!pending_removals.empty()){commit();++n;continue;}
            if(next.empty()){stop=Stop::Idle;break;}
            round.swap(next);next.clear();cursor=0;++round_no;++c.promotions;++n;log("新一輪開始，成員固定 ",round.size()," 項");
        }
        const double elapsed=clock.ms()-start;in_slice=false;
        if(stop==Stop::Count||stop==Stop::Time)log("讓出（",stop_name(stop),"）：已執行",n,"項，耗時",elapsed,"ms，下一輪",next.size(),
            "項，未提交結構",pending_removals.size(),"項",cursor<round.size()?"；本輪未完成不提交":"");
        return {stop,n,elapsed};
    }
private:
    void complete(const Completion& m){
        auto it=std::find_if(ops.begin(),ops.end(),[&](const Op& o){return o.id==m.op;});
        if(it==ops.end()||it->s==OpState::Settled){++c.completions_unknown;log("完成#",m.op," 無對應未結操作（重複或未知），不套用");return;}
        it->s=OpState::Settled;++c.completions_admitted;if(it->lease){--service.leases;it->lease=false;}
        log("操作#",m.op," 完成已收（",m.cancel_seen?"工作端看到取消":"工作端未理會取消","），租用釋放 → 租用數",service.leases);
        auto t=targets.find(it->target);
        if(scope==ScopeState::Running&&t!=targets.end()&&t->second.s==TargetState::Live&&t->second.gen==it->gen){t->second.value=m.value;log("結果套用到目標",it->target);}
        else{++c.completions_rejected_stale_target;log("拒絕回寫：目標",it->target,"已失效、世代不符或範圍收尾中");}
    }
    void dispatch(const Work& w){
        ++depth;max_depth=std::max(max_depth,depth);
        if(w.kind==Work::Notice){
            // Ordinary UI subscriptions are detached by close: a queued notice is not delivered after close
            // (the callback that itself called close() still returns normally).
            if(scope!=ScopeState::Running){++c.notices_suppressed;log("通知 ",w.key," 不遞送：範圍已關閉中，訂閱已解除（計為抑制，不是遺失事件）");}
            else{++c.notices_dispatched;log("派送通知 ",w.key,"，讀值=",value[w.key]);
                if(auto f=on_notice.find(w.key);f!=on_notice.end())f->second(*this);}
        }else{
            auto t=targets.find(w.target);
            if(t!=targets.end()&&t->second.s==TargetState::Live&&t->second.gen==w.gen){++c.events_executed;log("執行事件#",w.id,"→ 目標",w.target);if(on_event)on_event(*this,w.target);}
            else{++c.events_rejected_delivery;log("拒絕遞送事件#",w.id,"：目標",w.target,"待移除／已移除／世代不符（事件已記錄，未丟棄）");}
        }
        --depth;
    }
    void enqueue_notice(char k){
        // Fixture: one pending next-round notice per key; a same-key entry still later in the CURRENT round is not
        // consumed (dirty-consumption policy undefined).
        for(auto& w:next)if(w.kind==Work::Notice&&w.key==k){++c.notices_coalesced;log(k," 的下一輪通知已存在，合併");return;}
        next.push_back({Work::Notice,k,0,0,0});++c.notices_enqueued;log(k," 通知排入下一輪");
    }
    void commit(){
        for(int t:pending_removals){targets[t].s=TargetState::Removed;log("提交：實體移除目標 ",t);}
        pending_removals.clear();++c.commits;
    }
};

// Host: admits at most `admit_limit` mailbox messages per step (routing by env id; unknown ids counted), then
// gives one environment a slice in round-robin order. Fixture policy, not a production queue policy.
struct Host {
    std::vector<Env*> envs; std::shared_ptr<Mailbox> mailbox=std::make_shared<Mailbox>(); size_t rr=0;
    size_t admit_limit=8; int unknown_env_messages=0;
    Env* step(Budget b,SliceResult* out=nullptr){
        for(auto& m:mailbox->take(admit_limit)){bool routed=false;for(auto* e:envs)if(e->id==m.env){e->admit_completion(m);routed=true;}if(!routed)++unknown_env_messages;}
        for(auto* e:envs)e->tick();
        for(size_t k=0;k<envs.size();++k){Env* e=envs[(rr+k)%envs.size()];
            if(e->has_work()){rr=(rr+k+1)%envs.size();auto r=e->slice(b);if(out)*out=r;e->tick();return e;}}
        return nullptr;
    }
    bool idle(){for(auto* e:envs)if(e->has_work())return false;return mailbox->size()==0;}
};

} // namespace sched
