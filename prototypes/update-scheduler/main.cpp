// THROWAWAY prototype shell: --scenarios (simulated clock, self-checking), --benchmark (steady_clock, native
// threads), --interactive (small Win32 window). The scheduling logic lives in scheduler_model.hpp.
#include "scheduler_model.hpp"
#include <windows.h>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace sched;

// ---------- deterministic sessions (shared by --scenarios and --interactive) ----------
struct Session {
    SimClock clock; Trace trace; Service service;
    Env A{1,"環境A",clock,trace,service}, B{2,"環境B",clock,trace,service};
    Host host; Budget budget{1000,2}; std::string name;
    int observed_b_first=-1,observed_b_next=-1; bool c_removed_when_b_notified=false,post_to_c_rejected=false;
    explicit Session(size_t trace_limit):trace(trace_limit){host.envs={&A,&B};B.service_lease_held=true;++service.leases;B.add_target(9);}
    Env* step(double advance_ms=0,SliceResult* r=nullptr){clock.advance(advance_ms);Env* e=host.step(budget,r);
        if(!e)trace.add("（沒有環境有工作；只檢查收尾狀態）");return e;}
};
static void build_s1(Session& s){
    s.name="情境1：回呼改值並移除C，輪中額度讓出";s.budget={1000,2};
    auto& A=s.A;A.value['A']=0;A.value['B']=0;A.add_target(3);A.add_target(4);
    A.on_notice['A']=[&s](Env& e){e.set('B',1);s.observed_b_first=e.value['B'];e.request_remove(3);s.post_to_c_rejected=!e.post_event(3);e.set('B',2);};
    A.on_notice['B']=[&s](Env& e){s.observed_b_next=e.value['B'];s.c_removed_when_b_notified=e.targets[3].s==Env::TargetState::Removed;};
    A.set('A',1);A.set('X',1);A.post_event(3);A.post_event(4);
}
static void build_s2(Session& s,int variant){
    s.name="情境2：工作數／時間額度、長回呼與重入";
    auto& A=s.A;
    for(char k:std::string("123456"))A.on_notice[k]=[&s](Env&){s.clock.advance(1);};
    A.on_notice['L']=[&s](Env&){s.clock.advance(7);};                     // long callback: soft limit
    A.on_notice['R']=[](Env& e){e.slice({1000,100});e.slice({1000,100});};  // recursive run attempts
    if(variant==0){s.budget={100,3};for(char k:std::string("123456"))A.set(k,1);}
    if(variant==1){s.budget={2.5,100};for(char k:std::string("123456"))A.set(k,1);}
    if(variant==2){s.budget={2,100};A.set('L',1);A.set('1',1);}
    if(variant==3){s.budget={1000,100};A.set('R',1);}
}
static int op_c=0,op_late=0;
static void build_s3(Session& s){
    s.name="情境3：A操作中關閉、收尾逾期、B持續";s.budget={1000,4};
    auto& A=s.A;A.add_target(3);op_c=A.start_op(3);A.request_remove(3);
}

// ---------- checks ----------
static int failures=0;
static void check(bool ok,const char* what,std::ostream& out){out<<(ok?"PASS ":"FAIL ")<<what<<'\n';if(!ok)++failures;}
static void dump(Session& s,std::ostream& out){for(auto& l:s.trace.lines)out<<"  "<<l<<'\n';s.trace.lines.clear();}
static void run_until_idle(Session& s,int max_steps=200){for(int i=0;i<max_steps&&!s.host.idle();++i)s.step();}
// Everything an ordinary entry point could change.
struct EnvSnapshot{std::map<char,int> value;std::vector<std::pair<int,int>> targets;size_t round,cursor,next,inbox,removals,ops;int leases,events;
    bool operator==(const EnvSnapshot&) const=default;};
static EnvSnapshot snap(Env& e){EnvSnapshot s{e.value,{},e.round.size(),e.cursor,e.next.size(),e.inbox.size(),e.pending_removals.size(),e.ops.size(),e.service.leases,e.c.events_accepted};
    for(auto& [t,tg]:e.targets)s.targets.push_back({t,(int)tg.gen*10+(int)tg.s});return s;}

static void scenarios(std::ostream& out){
    out<<"模擬時鐘（不是效能量測）。每個 PASS/FAIL 由模型實際狀態判定。每片的輪開始、提交、完成處理都各算一項工作。\n";
    {   Session s(100000);build_s1(s);out<<"\n== "<<s.name<<" ==\n";
        bool midround_commit=false,midround_yield_seen=false;
        for(int i=0;i<20&&!s.host.idle();++i){
            SliceResult r{};s.step(0,&r);
            const bool mid=s.A.round_no==1&&s.A.cursor<s.A.round.size();
            if(mid&&(r.stop==Stop::Count||r.stop==Stop::Time)){midround_yield_seen=true;if(s.A.c.commits>0||s.A.targets[3].s==Env::TargetState::Removed)midround_commit=true;}
            if(i==0){
                check(s.observed_b_first==1,"回呼內設定 B=1 後立即讀到 1",out);
                check(s.A.targets[3].s==Env::TargetState::PendingRemoval,"移除受理後 C 立即為待移除",out);
                check(s.post_to_c_rejected,"待移除的 C 拒絕新操作",out);
                check(s.A.c.notices_coalesced==1&&s.A.next.size()==1,"B 兩次設定合併為一個下一輪通知",out);
            }
        }
        dump(s,out);
        check(midround_yield_seen&&!midround_commit,"第1輪中途曾因額度讓出，且讓出時 C 尚未提交",out);
        check(s.A.c.events_rejected_delivery==1&&s.A.c.events_executed==1,"C 的事件拒絕遞送、D 的事件執行",out);
        check(s.observed_b_next==2&&s.c_removed_when_b_notified,"下一輪 B 通知讀到 2，且 C 的移除已在其前提交",out);
        check(s.A.c.events_accepted==s.A.c.events_executed+s.A.c.events_rejected_delivery&&s.A.c.events_discarded==0&&s.A.pending_work()==0,"已受理事件 = 執行 + 拒絕遞送；丟棄 0；無剩餘",out);
        check(s.A.max_depth==1,"沒有遞迴派送（最大深度 1）",out);
    }
    {   Session s(100000);s.name="情境1b（fixture 觀察，不是政策）：本輪已有 B 通知時回呼再設 B";out<<"\n== "<<s.name<<" ==\n";
        auto& A=s.A;A.value['B']=0;int seen=0;A.on_notice['A']=[](Env& e){e.set('B',5);};A.on_notice['B']=[&](Env&){++seen;};
        A.set('A',1);A.set('B',1);s.budget={1000,100};run_until_idle(s);dump(s,out);
        out<<"觀察：B 通知被派送 "<<seen<<" 次（本輪那次讀到 5，下一輪再一次）。現行 fixture 不消耗本輪標記；去重政策未定，未測試。\n";
    }
    for(int v=0;v<4;++v){
        Session s(100000);build_s2(s,v);out<<"\n== "<<s.name<<" 變體"<<v<<" ==\n";
        auto r=s.A.slice(s.budget);dump(s,out);
        if(v==0)check(r.stop==Stop::Count&&r.executed==3&&s.A.c.notices_dispatched==2,"工作數額度 3：輪開始 1 項＋通知 2 項後讓出",out);
        if(v==1)check(r.stop==Stop::Time&&s.A.c.notices_dispatched==3&&r.elapsed>=2.5,"時間額度 2.5ms：在工作邊界檢查，第 3 個通知（3ms）後讓出",out);
        if(v==2)check(r.stop==Stop::Time&&s.A.c.notices_dispatched==1&&r.elapsed==7,"長回呼 7ms 超過 2ms 額度但正常返回（軟限制）",out);
        if(v==3){
            check(s.A.wake_requests==2&&s.A.wake_pending&&s.A.max_depth==1,"回呼內兩次重入要求：不遞迴，只留一個待喚醒",out);
            auto r2=s.A.slice(s.budget);dump(s,out);
            check(r2.stop==Stop::Idle&&!s.A.wake_pending,"待喚醒只造成一次續排，沒有忙碌重排",out);
        }
    }
    {   Session s(100000);out<<"\n== 情境2b：無效額度參數 ==\n";bool threw=false;
        try{s.A.slice({0,4});}catch(const std::invalid_argument&){threw=true;}
        bool threw2=false;try{s.A.slice({1,0});}catch(const std::invalid_argument&){threw2=true;}
        check(threw&&threw2&&!s.A.in_slice,"0 毫秒或 0 項的額度被拒絕，不會無聲空轉",out);
    }
    {   Session s(100000);build_s3(s);out<<"\n== "<<s.name<<" ==\n";
        auto& A=s.A;auto& B=s.B;
        run_until_idle(s);A.reuse_target(3);
        s.host.mailbox->post({1,op_c,42,false});run_until_idle(s);
        check(A.targets[3].value==0&&A.c.completions_rejected_stale_target==1,"舊世代結果被拒絕，不回寫到重新建立的 C",out);
        // start_op invalid targets on Running B: no lease, no op.
        const int leases0=s.service.leases;const size_t ops0=B.ops.size();
        B.add_target(10);B.request_remove(10);
        const bool missing=B.start_op(77)==0,pending=B.start_op(10)==0;run_until_idle(s);const bool removed=B.start_op(10)==0;
        check(missing&&pending&&removed&&s.service.leases==leases0&&B.ops.size()==ops0,"start_op 對不存在／待移除／已移除目標都拒絕，不取得租用",out);
        op_late=A.start_op(3);
        check(op_late!=0&&s.service.leases==2,"共用服務租用：B 1 + A 操作 1",out);
        A.close(5);
        const EnvSnapshot before=snap(A);const int rejected0=A.c.actions_rejected;
        const bool all_rejected=!A.set('A',9)&&!A.add_target(20)&&!A.reuse_target(3)&&!A.post_event(3)&&!A.request_remove(3)&&A.start_op(3)==0;
        check(all_rejected&&snap(A)==before&&A.c.actions_rejected==rejected0+6,"收尾中 6 個普通入口全部拒絕：值、目標、佇列、操作、租用都不變",out);
        B.post_event(9);for(int i=0;i<10&&B.c.events_executed<1;++i)s.step(0.5);
        check(B.c.events_executed==1&&A.scope==Env::ScopeState::Closing,"A 收尾中，B 仍處理事件",out);
        s.step(6);s.step();dump(s,out);
        check(A.scope==Env::ScopeState::Closing&&A.overdue&&A.outstanding()==1,"診斷期限已過：收尾逾期，但仍是收尾中（非已關閉）",out);
        check(s.service.leases==2,"未結操作仍持有租用；服務未被釋放",out);
        s.host.mailbox->post({1,op_late,7,false});run_until_idle(s);dump(s,out);
        check(A.c.completions_admitted==2&&A.outstanding()==0,"收尾中仍接受完成通道（取消已請求但工作端未理會）",out);
        check(A.scope==Env::ScopeState::Closed,"操作與租用結算後 A 才關閉",out);
        B.post_event(9);run_until_idle(s);
        check(B.c.events_executed==2&&s.service.leases==1,"A 關閉後 B 繼續；共用服務仍由 B 租用",out);
        dump(s,out);
    }
    {   Session s(100000);s.name="情境4：信箱灌爆＋B 持續＋A 輪中";s.budget={1000,4};s.host.admit_limit=8;out<<"\n== "<<s.name<<" ==\n";
        auto& A=s.A;auto& B=s.B;A.add_target(5);int real=A.start_op(5);A.add_target(6);
        for(char k:std::string("abcdef"))A.set(k,1);
        A.on_notice['a']=[](Env& e){e.request_remove(6);};
        for(int i=0;i<39;++i)s.host.mailbox->post({1,1000+i,0,false});s.host.mailbox->post({1,real,1,false});
        for(int i=0;i<20;++i)B.post_event(9);
        int max_a=0,max_admit=0;bool midround_commit=false;size_t box=s.host.mailbox->size();
        for(int i=0;i<200&&!s.host.idle();++i){
            SliceResult r{};Env* e=s.step(0,&r);size_t now=s.host.mailbox->size();max_admit=std::max<int>(max_admit,(int)(box-now));box=now;
            if(e==&A){max_a=std::max(max_a,r.executed);if(A.round_no==1&&A.cursor<A.round.size()&&A.c.commits>0)midround_commit=true;}
        }
        s.trace.lines.clear();
        check(max_a<=4&&max_admit<=8,"每片 A 最多 4 項（含完成處理）；每步最多收 8 封信",out);
        check(A.c.completions_unknown==39&&A.c.completions_admitted==1,"39 封未知完成被計數拒絕，1 封真的完成被接受",out);
        check(B.c.events_executed==20,"灌爆期間 B 的 20 個事件全部執行",out);
        check(!midround_commit&&A.targets[6].s==Env::TargetState::Removed,"A 輪中讓出不提交；輪結束後才提交",out);
    }
    {   Session s(100000);s.name="情境5：額度在輪尾用完、外層遍歷擋住提交";out<<"\n== "<<s.name<<" ==\n";
        auto& A=s.A;A.add_target(7);A.request_remove(7);int commits_seen_by_z=-1;
        // Round 1: notice '1' accepts a new removal (target 8); notice '2' sets Z (next round).
        A.on_notice['1']=[](Env& e){e.add_target(8);e.request_remove(8);};A.on_notice['2']=[](Env& e){e.set('Z',1);};
        A.on_notice['Z']=[&](Env& e){commits_seen_by_z=e.c.commits;};
        A.set('1',1);A.set('2',1);
        auto r1=A.slice({1000,1});  // commit of target 7 (setup removal) only
        auto r2=A.slice({1000,3});  // promotion + '1' + '2' -> round complete, budget exhausted at round end
        dump(s,out);
        check(r1.executed==1&&A.c.commits==1,"設定時的移除先提交（1 項）",out);
        check(r2.stop==Stop::Count&&A.cursor==A.round.size()&&A.round_no==1&&A.c.commits==1&&!A.pending_removals.empty()&&A.next.size()==1,
              "輪尾額度用完：本輪已完成，但尚未提交，也沒有開始下一輪",out);
        A.outer_traversals=1;auto r3=A.slice({1000,10});dump(s,out);
        check(r3.stop==Stop::CommitHeld&&r3.executed==0&&A.c.commits==1&&A.round_no==1,"外層遍歷仍在：不提交、不開始下一輪",out);
        A.outer_traversals=0;auto r4=A.slice({1000,10});dump(s,out);
        check(A.c.commits==2&&commits_seen_by_z==2&&A.round_no==2&&r4.stop==Stop::Idle,"外層結束後：先提交，再開始下一輪；Z 通知看到已提交",out);
    }
    {   Session s(100000);s.name="情境6：關閉後不再遞送通知、關閉收尾受外層遍歷保護";out<<"\n== "<<s.name<<" ==\n";
        {   auto& A=s.A;int sentinel=0;A.on_notice['q']=[&](Env&){++sentinel;};A.set('q',1);
            A.close(5);run_until_idle(s);dump(s,out);
            check(sentinel==0&&A.c.notices_suppressed==1&&A.scope==Env::ScopeState::Closed,"6a 關閉前已排入的通知不遞送（計為抑制），範圍照常收尾並關閉",out);
        }
        {   Session t(100000);auto& A=t.A;int sentinel=0;bool returned=false;
            A.on_notice['c']=[&](Env& e){e.close(5);returned=true;};A.on_notice['d']=[&](Env&){++sentinel;};
            A.set('c',1);A.set('d',1);t.budget={1000,100};run_until_idle(t);dump(t,out);
            check(returned&&sentinel==0&&A.c.notices_dispatched==1&&A.c.notices_suppressed==1&&A.scope==Env::ScopeState::Closed,
                  "6b 回呼內關閉：該回呼正常返回，同輪後面的通知不遞送",out);
        }
        {   Session t(100000);auto& A=t.A;A.service_lease_held=true;++t.service.leases;const int leases=t.service.leases;
            A.outer_traversals=1;A.close(5);run_until_idle(t);t.step();
            check(A.scope==Env::ScopeState::Closing&&t.service.leases==leases,"6c 無目標環境但外層遍歷仍在：保持收尾中，環境租用保留",out);
            A.outer_traversals=0;t.step();dump(t,out);
            check(A.scope==Env::ScopeState::Closed&&t.service.leases==leases-1,"6c 外層遍歷結束後才關閉並釋放環境租用",out);
        }
    }
    out<<"\nscenarios "<<(failures?"FAIL":"PASS")<<" failures="<<failures<<'\n';
}

// ---------- benchmark (real steady_clock, native threads) ----------
static void spin_us(double us){auto end=std::chrono::steady_clock::now()+std::chrono::duration<double,std::micro>(us);while(std::chrono::steady_clock::now()<end){}}
static double pct(std::vector<double> v,double p){std::sort(v.begin(),v.end());return v[std::min(v.size()-1,static_cast<size_t>(p*(v.size()-1)+0.5))];}
static std::string stats(const std::vector<double>& v){std::ostringstream o;o<<std::fixed<<std::setprecision(3)<<pct(v,.5)<<'/'<<pct(v,.95)<<'/'<<*std::max_element(v.begin(),v.end());return o.str();}
struct BenchEnv { SteadyClock clock; Trace trace{1}; Service service; Env A{1,"A",clock,trace,service},B{2,"B",clock,trace,service}; Host host;
    BenchEnv(){A.verbose=false;B.verbose=false;host.envs={&A,&B};A.add_target(1);B.add_target(1);} };
static bool settled(Env& e){return e.c.events_accepted==e.c.events_executed+e.c.events_rejected_delivery&&e.pending_work()==0&&e.pending_removals.empty();}

static void benchmark(std::ostream& summary,std::ostream& csv){
    summary<<"v3. steady_clock CPU wall time. Work cost = controlled busy-wait (synthetic). Trace formatting disabled in slices; files written after runs.\n"
             "Slice time covers: boundary checks, completion processing, notice/event dispatch (incl. rejected delivery), commit, round promotion.\n"
             "Host step time additionally covers bounded mailbox admission (<= admit_limit) and tick bookkeeping.\n"
             "40 A slices are SAMPLED per case; afterwards leftover work is drained OUTSIDE measurement and event accounting is asserted.\n";
    csv<<"case,budget_ms,budget_count,cost_us,slice,slice_ms,host_step_ms,executed,stop,b_executed_total\n";
    struct Case{const char* name;double ms;int count;double cost;};
    for(auto k:std::vector<Case>{{"hybrid",0.5,4,50},{"hybrid",0.5,16,50},{"hybrid",2,4,50},{"hybrid",2,16,50},{"zero_cost_overhead",2,16,0}}){
        BenchEnv b;double cost=k.cost;
        b.A.on_event=[&](Env&,int){spin_us(cost);};b.B.on_event=[&](Env&,int){spin_us(cost);};
        for(int i=0;i<2000;++i){b.A.post_event(1);b.B.post_event(1);}
        for(int i=0;i<20;++i)b.host.mailbox->post({1,5000+i,0,false});  // unknown completions: charged work
        std::vector<double> el,host_el,ex;std::map<Stop,int> stops;int slices=0;
        while(slices<40){
            SliceResult r{};auto t0=std::chrono::steady_clock::now();Env* e=b.host.step({k.ms,k.count},&r);
            double hs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();if(!e)break;
            if(e==&b.A){++slices;el.push_back(r.elapsed);host_el.push_back(hs);ex.push_back(r.executed);++stops[r.stop];
                csv<<k.name<<','<<k.ms<<','<<k.count<<','<<cost<<','<<slices<<','<<r.elapsed<<','<<hs<<','<<r.executed<<','<<stop_name(r.stop)<<','<<b.B.c.events_executed<<'\n';}
        }
        const int pending_after_sample=(int)b.A.pending_work();
        for(int i=0;i<100000&&!b.host.idle();++i)b.host.step({k.ms,k.count});  // drain outside measurement
        const bool ok=slices==40&&settled(b.A)&&settled(b.B)&&b.A.c.completions_unknown==20;
        if(!ok)++failures;
        summary<<std::fixed<<std::setprecision(3)<<"\n"<<k.name<<" budget="<<k.ms<<"ms/"<<k.count<<" cost="<<cost<<"us sampled_A_slices="<<slices
               <<"\n  slice ms p50/p95/max: "<<stats(el)<<"   host step ms p50/p95/max: "<<stats(host_el)
               <<"\n  items per slice p50/p95/max: "<<stats(ex)<<"  stop reasons: count="<<stops[Stop::Count]<<" time="<<stops[Stop::Time]
               <<"\n  A pending after sampling="<<pending_after_sample<<"; after drain: A accepted="<<b.A.c.events_accepted<<" executed="<<b.A.c.events_executed
               <<" rejected="<<b.A.c.events_rejected_delivery<<" pending="<<b.A.pending_work()<<"; B executed="<<b.B.c.events_executed<<" unknown completions charged="<<b.A.c.completions_unknown
               <<(ok?"  [PASS accounting]":"  [FAIL accounting]")<<'\n';
    }
    {   // Dedicated long-callback demo, not part of the percentile cohorts above.
        BenchEnv b;bool long_done=false;b.A.on_event=[&](Env&,int){if(!long_done){long_done=true;spin_us(5000);}else spin_us(50);};
        for(int i=0;i<40;++i)b.A.post_event(1);
        auto r1=b.A.slice({0.5,16});auto r2=b.A.slice({0.5,16});
        for(int i=0;i<1000&&!b.host.idle();++i)b.host.step({0.5,16});
        const bool ok=r1.elapsed>=5.0&&r1.stop==Stop::Time&&settled(b.A);if(!ok)++failures;
        summary<<"\nlong_callback_demo (separate, not a cohort): budget 0.5ms/16, first event spins 5ms\n  slice1 "<<r1.elapsed<<"ms executed="<<r1.executed<<" stop="<<stop_name(r1.stop)
               <<"; slice2 "<<r2.elapsed<<"ms executed="<<r2.executed<<"; accounting after drain "<<(ok?"PASS":"FAIL")<<'\n';
    }
}

// Native-thread close demonstration: bounded workers post completions through the mailbox (ids only).
static void native_close(std::ostream& out){
    SteadyClock clock;Trace trace(100000);Service service;Env A{1,"環境A",clock,trace,service},B{2,"環境B",clock,trace,service};Host host;host.envs={&A,&B};
    B.service_lease_held=true;++service.leases;A.add_target(1);B.add_target(9);
    struct Worker{std::thread t;std::shared_ptr<std::atomic<bool>> cancel=std::make_shared<std::atomic<bool>>(false),finished=std::make_shared<std::atomic<bool>>(false);};
    auto launch=[&](int op,bool cooperative,int steps){Worker w;auto mb=host.mailbox;auto c=w.cancel,f=w.finished;
        w.t=std::thread([mb,c,f,op,cooperative,steps]{bool seen=false;for(int i=0;i<steps;++i){if(c->load()){seen=true;if(cooperative)break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            mb->post({1,op,op*10,seen});f->store(true);});return w;};
    int op1=A.start_op(1),op2=A.start_op(1);
    std::vector<Worker> workers;workers.push_back(launch(op1,false,60));workers.push_back(launch(op2,true,60));
    out<<"worker op"<<op1<<"：故意忽略取消，迴圈 60 次 sleep_for(1ms)；worker op"<<op2<<"：合作式，看到取消就提早完成。兩者都有限、都不碰 UI 指標。\n";
    const double start=clock.ms();bool closed_requested=false,watchdog_reported=false;
    int b_at_close=-1,b_while_closing=0,leases_while_overdue=-1;double t_close=0,t_overdue=0,t_closed=0;
    auto all_finished=[&]{for(auto& w:workers)if(!w.finished->load())return false;return true;};
    // Owner keeps scheduling (B work + A cleanup) until A is Closed AND every worker reported finished.
    while(!(A.scope==Env::ScopeState::Closed&&all_finished())){
        if(!closed_requested&&clock.ms()-start>=5){A.close(20);for(auto& w:workers)w.cancel->store(true);closed_requested=true;t_close=clock.ms()-start;b_at_close=B.c.events_executed;}
        B.post_event(9);host.step({1,8});
        if(closed_requested&&A.scope==Env::ScopeState::Closing)b_while_closing=B.c.events_executed-b_at_close;
        if(A.overdue&&leases_while_overdue<0){leases_while_overdue=service.leases;t_overdue=clock.ms()-start;}
        if(A.scope==Env::ScopeState::Closed&&t_closed==0)t_closed=clock.ms()-start;
        if(!watchdog_reported&&clock.ms()-start>5000){watchdog_reported=true;out<<"診斷：5 秒仍未完成（只回報，不強制釋放或分離；繼續排程）\n";}
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    for(auto& w:workers)w.t.join();  // only after finished flags are true
    while(!host.idle())host.step({1,8});
    out<<std::fixed<<std::setprecision(1)<<"觀察時間：關閉要求 "<<t_close<<"ms、逾期 "<<t_overdue<<"ms、已關閉 "<<t_closed<<"ms（實際觀察值，受 OS 排程影響）\n"
       <<"A 完成收到="<<A.c.completions_admitted<<" 拒絕回寫失效目標次數="<<A.c.completions_rejected_stale_target<<" 失效目標值="<<A.targets[1].value
       <<" B 在 A 收尾期間實際執行事件="<<b_while_closing<<" 逾期時共用服務租用="<<leases_while_overdue<<" 結束時租用="<<service.leases<<'\n';
    for(auto& l:trace.lines)if(l.find("環境A")!=std::string::npos)out<<"  "<<l<<'\n';
    const bool ok=A.scope==Env::ScopeState::Closed&&A.c.completions_admitted==2&&A.c.completions_rejected_stale_target==2&&A.targets[1].value==0
        &&b_while_closing>0&&leases_while_overdue==2&&service.leases==1&&A.overdue;
    if(!ok)++failures;out<<(ok?"PASS":"FAIL")<<" native close（只在 finished 後 join；逾期時一個操作仍持有租用）\n";
}

// ---------- interactive Win32 ----------
static std::unique_ptr<Session> live;static int live_kind=1;static HWND list_box,status_box;
static void reset_live(){live=std::make_unique<Session>(400);if(live_kind==1)build_s1(*live);else if(live_kind==2)build_s2(*live,3);else build_s3(*live);}
static std::wstring widen(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n);return w;}
static const char* scope_name(Env::ScopeState s){return s==Env::ScopeState::Running?"執行中":s==Env::ScopeState::Closing?"收尾中":"已關閉";}
static const char* target_name(Env& e,int t){auto it=e.targets.find(t);if(it==e.targets.end())return "無";return it->second.s==Env::TargetState::Live?"有效":it->second.s==Env::TargetState::PendingRemoval?"待移除":"已移除";}
static void refresh(){
    auto& s=*live;auto& A=s.A;std::ostringstream o;o.setf(std::ios::fixed);o.precision(1);
    o<<s.name<<"  模擬時間 "<<s.clock.t<<"ms  額度 "<<s.budget.ms<<"ms/"<<s.budget.count<<"項\r\n"
     <<"A值 A="<<A.value['A']<<" B="<<A.value['B']<<"  B環境 事件執行="<<s.B.c.events_executed<<"\r\n"
     <<"輪"<<A.round_no<<" 游標 "<<A.cursor<<"/"<<A.round.size()<<" 下一輪 "<<A.next.size()<<" 項  待處理完成 "<<A.inbox.size()<<"  待喚醒="<<A.wake_pending<<"\r\n"
     <<"目標C："<<target_name(A,3)<<"  範圍："<<scope_name(A.scope)<<(A.overdue?"（逾期）":"")<<"  未結操作 "<<A.outstanding()<<"\r\n"
     <<"事件 受理 "<<A.c.events_accepted<<" 執行 "<<A.c.events_executed<<" 拒絕遞送 "<<A.c.events_rejected_delivery<<" 丟棄 "<<A.c.events_discarded
     <<"  拒絕普通操作 "<<A.c.actions_rejected<<"  共用服務租用 "<<s.service.leases;
    SetWindowTextW(status_box,widen(o.str()).c_str());
    SendMessageW(list_box,LB_RESETCONTENT,0,0);
    for(auto& l:s.trace.lines)SendMessageW(list_box,LB_ADDSTRING,0,(LPARAM)widen(l).c_str());
    SendMessageW(list_box,LB_SETTOPINDEX,s.trace.lines.empty()?0:s.trace.lines.size()-1,0);
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COMMAND){
        auto& s=*live;
        switch(LOWORD(w)){
        case 1:case 2:case 3:live_kind=LOWORD(w);reset_live();break;
        case 10:s.step(1);break;
        case 11:s.A.close(3);break;
        case 12:for(auto& o:s.A.ops)if(o.s!=Env::OpState::Settled){s.host.mailbox->post({1,o.id,99,false});s.trace.add("（使用者送達操作#"+std::to_string(o.id)+" 的完成，下一次單步時由擁有者執行緒收取）");break;}
                break;
        case 13:if(s.A.start_op(3)==0)s.A.post_event(3);break;
        case 14:reset_live();break;
        case 15:s.B.post_event(9);break;  // dispatched on the next step
        }
        refresh();return 0;
    }
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static int interactive(){
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"UpdateSchedulerPrototype";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    HWND win=CreateWindowExW(0,wc.lpszClassName,L"可丟棄原型：更新排程（模擬時鐘）",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1100,760,nullptr,nullptr,wc.hInstance,nullptr);
    // Two rows of buttons so all nine fit inside the client area.
    const wchar_t* labels[]={L"情境1",L"情境2",L"情境3",L"重設",L"單步一片",L"關閉A",L"送達完成",L"A啟動操作/送事件",L"B送事件"};int ids[]={1,2,3,14,10,11,12,13,15};
    for(int i=0;i<9;++i){const int row=i<4?0:1,col=i<4?i:i-4;
        CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,10+col*160,10+row*36,152,30,win,(HMENU)(INT_PTR)ids[i],wc.hInstance,nullptr);}
    status_box=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,10,86,1060,110,win,nullptr,wc.hInstance,nullptr);
    list_box=CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_BORDER|LBS_NOINTEGRALHEIGHT,10,204,1060,500,win,nullptr,wc.hInstance,nullptr);
    reset_live();refresh();
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    live.reset();return 0;
}

int main(int argc,char** argv){
    SetConsoleOutputCP(CP_UTF8);
    std::string mode=argc>1?argv[1]:"";
    try{
        if(mode=="--scenarios"){std::ofstream f("scenarios-v3.log",std::ios::binary);scenarios(f);f.close();std::cout<<"scenarios failures="<<failures<<" -> scenarios-v3.log\n";return failures?1:0;}
        if(mode=="--benchmark"){std::ofstream s("benchmark-v3-summary.log",std::ios::binary),c("benchmark-v3-slices.csv",std::ios::binary);benchmark(s,c);
            s<<"\n== native thread close ==\n";native_close(s);s<<"\nbenchmark failures="<<failures<<'\n';
            std::cout<<"benchmark failures="<<failures<<" -> benchmark-v3-summary.log / benchmark-v3-slices.csv\n";return failures?1:0;}
        if(mode=="--interactive")return interactive();
        std::cerr<<"use --scenarios | --benchmark | --interactive\n";return 2;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
