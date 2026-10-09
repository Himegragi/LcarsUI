// THROWAWAY prototype shell v5: --scenarios (simulated clock, self-checking), --benchmark (steady_clock, native
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
    Env A,B; Host host; Budget budget{1000,2}; std::string name;
    int observed_b_first=-1,observed_b_next=-1; bool c_removed_when_b_notified=false,post_to_c_rejected=false;
    explicit Session(size_t trace_limit,size_t a_cap=64,size_t a_term=8,size_t b_cap=64,size_t b_term=8)
        :trace(trace_limit),A(1,"環境A",clock,trace,service,a_cap,a_term),B(2,"環境B",clock,trace,service,b_cap,b_term){
        host.envs={&A,&B};B.service_lease_held=true;++service.leases;B.add_target(9);}
    Env* step(double advance_ms=0,SliceResult* r=nullptr){clock.advance(advance_ms);Env* e=host.step(budget,r);
        if(!e)trace.add("（沒有環境有工作；只檢查收尾狀態）");return e;}
};
static int failures=0;
static void check(bool ok,const std::string& what,std::ostream& out){out<<(ok?"PASS ":"FAIL ")<<what<<'\n';if(!ok)++failures;}
static void dump(Session& s,std::ostream& out){for(auto& l:s.trace.lines)out<<"  "<<l<<'\n';s.trace.lines.clear();}
static void run_until_idle(Session& s,int max_steps=100000){for(int i=0;i<max_steps&&!s.host.idle();++i)s.step();}
// Independent scan of the queues (the model's pending counter must agree with it).
static size_t scan_pending(Env& e){size_t n=0;for(size_t i=e.cursor;i<e.round.size();++i)n+=e.round[i].kind==Work::Event;for(auto& w:e.next)n+=w.kind==Work::Event;return n;}
static bool accounted(Env& e){const size_t scanned=scan_pending(e);return scanned==e.pending_events()&&e.c.events_accepted==e.c.events_executed+e.c.events_rejected_delivery+(int)scanned;}
struct EnvSnapshot{std::map<char,int> value;std::vector<std::pair<int,int>> targets;size_t round,cursor,next,removals,ledger_used,ready;int leases,events;
    bool operator==(const EnvSnapshot&) const=default;};
static EnvSnapshot snap(Env& e){EnvSnapshot s{e.value,{},e.round.size(),e.cursor,e.next.size(),e.pending_removals.size(),e.ledger->used(),e.ledger->ready(),e.service.leases,e.c.events_accepted};
    for(auto& [t,tg]:e.targets)s.targets.push_back({t,(int)tg.gen*10+(int)tg.s});return s;}

// ---- builders reused by the GUI ----
static void build_s1(Session& s){
    s.name="情境1：回呼改值並移除C，輪中額度讓出";s.budget={1000,2};
    auto& A=s.A;A.value['A']=0;A.value['B']=0;A.add_target(3);A.add_target(4);
    A.on_notice['A']=[&s](Env& e){e.set('B',1);s.observed_b_first=e.value['B'];e.request_remove(3);s.post_to_c_rejected=!e.post_event(3);e.set('B',2);};
    A.on_notice['B']=[&s](Env& e){s.observed_b_next=e.value['B'];s.c_removed_when_b_notified=e.targets[3].s==Env::TargetState::Removed;};
    A.set('A',1);A.set('X',1);A.post_event(3);A.post_event(4);
}
static Token gui_tokens[3];
static void build_overload(Session& s){s.name="引導：超載（A 普通容量 4）";s.budget={1000,2};s.A.add_target(3);for(int i=0;i<6;++i)s.A.post_event(3);}
static void build_mixed(Session& s){s.name="引導：混合流量（A 容量 8、終態 3、收尾配額 2、額度 1 項）";s.budget={1000,1};s.A.add_target(3);
    for(int i=0;i<3;++i){gui_tokens[i]=s.A.start_op(3);s.A.ledger->publish(gui_tokens[i],{i,false});}for(int i=0;i<6;++i)s.A.post_event(3);for(int i=0;i<4;++i)s.B.post_event(9);}
static void build_closing(Session& s){s.name="引導：滿載中關閉（A 容量 4、終態 3）";s.budget={1000,2};s.A.add_target(3);
    for(int i=0;i<4;++i)s.A.post_event(3);for(int i=0;i<3;++i)gui_tokens[i]=s.A.start_op(3);s.A.close(3);}

static void scenarios(std::ostream& out){
    out<<"v5 模擬時鐘（不是效能量測）。每個 PASS/FAIL 由模型實際狀態判定。工作單位：完成結算、通知／事件派送（含拒絕遞送）、提交、開始新一輪各算一項。\n";
    // ---- retained v3 scenarios (adapted to the slot ledger) ----
    {   Session s(100000);build_s1(s);out<<"\n== "<<s.name<<" ==\n";bool midround_commit=false,midround_yield=false;
        for(int i=0;i<20&&!s.host.idle();++i){SliceResult r{};s.step(0,&r);
            if(s.A.round_no==1&&s.A.cursor<s.A.round.size()&&r.stop==Stop::Count){midround_yield=true;if(s.A.c.commits>0)midround_commit=true;}
            if(i==0){check(s.observed_b_first==1,"回呼內 B=1 立即讀到 1",out);check(s.A.targets[3].s==Env::TargetState::PendingRemoval&&s.post_to_c_rejected,"C 立即待移除並拒絕新操作",out);}}
        dump(s,out);
        check(midround_yield&&!midround_commit,"輪中讓出時不提交",out);
        check(s.observed_b_next==2&&s.c_removed_when_b_notified,"下一輪 B 通知讀到 2，C 已在其前提交",out);
        check(accounted(s.A)&&s.A.pending_events()==0&&s.A.max_depth==1,"事件全部有處理結果；沒有遞迴派送",out);
    }
    for(int v=0;v<4;++v){Session s(100000);out<<"\n== 情境2 變體"<<v<<"：額度、長回呼、重入 ==\n";auto& A=s.A;
        for(char k:std::string("123456"))A.on_notice[k]=[&s](Env&){s.clock.advance(1);};
        A.on_notice['L']=[&s](Env&){s.clock.advance(7);};A.on_notice['R']=[](Env& e){e.slice({1000,100});e.slice({1000,100});};
        Budget b{100,3};if(v==0)for(char k:std::string("123456"))A.set(k,1);
        if(v==1){b={2.5,100};for(char k:std::string("123456"))A.set(k,1);}if(v==2){b={2,100};A.set('L',1);A.set('1',1);}if(v==3){b={1000,100};A.set('R',1);}
        auto r=A.slice(b);dump(s,out);
        if(v==0)check(r.stop==Stop::Count&&A.c.notices_dispatched==2,"工作數額度 3（輪開始＋2 通知）",out);
        if(v==1)check(r.stop==Stop::Time&&A.c.notices_dispatched==3,"時間額度在工作邊界讓出",out);
        if(v==2)check(r.stop==Stop::Time&&r.elapsed==7,"長回呼正常返回（軟限制）",out);
        if(v==3){check(A.wake_requests==2&&A.wake_pending&&A.max_depth==1,"重入只留一個待喚醒",out);auto r2=A.slice(b);check(r2.stop==Stop::Idle,"只續排一次",out);}
    }
    {   Session s(1000);out<<"\n== 情境5：輪尾額度用完、外層遍歷擋住提交 ==\n";auto& A=s.A;int seen=-1;
        A.on_notice['1']=[](Env& e){e.add_target(8);e.request_remove(8);};A.on_notice['2']=[](Env& e){e.set('Z',1);};A.on_notice['Z']=[&](Env& e){seen=e.c.commits;};
        A.set('1',1);A.set('2',1);auto r2=A.slice({1000,3});
        check(r2.stop==Stop::Count&&A.cursor==A.round.size()&&A.c.commits==0&&A.next.size()==1,"輪尾用完額度：不提交、不開始下一輪",out);
        A.outer_traversals=1;auto r3=A.slice({1000,10});check(r3.stop==Stop::CommitHeld&&A.c.commits==0,"外層遍歷仍在：不提交",out);
        A.outer_traversals=0;A.slice({1000,10});check(A.c.commits==1&&seen==1,"先提交再開始下一輪",out);
    }
    {   Session s(1000);out<<"\n== 情境6：關閉後不遞送通知；關閉收尾受外層遍歷保護 ==\n";
        {int sentinel=0;s.A.on_notice['q']=[&](Env&){++sentinel;};s.A.set('q',1);s.A.close(5);run_until_idle(s);
         check(sentinel==0&&s.A.c.notices_suppressed==1&&s.A.scope==Env::ScopeState::Closed,"6a 關閉前已排入的通知不遞送",out);}
        {Session t(1000);int sentinel=0;bool returned=false;t.A.on_notice['c']=[&](Env& e){e.close(5);returned=true;};t.A.on_notice['d']=[&](Env&){++sentinel;};
         t.A.set('c',1);t.A.set('d',1);t.budget={1000,100};run_until_idle(t);check(returned&&sentinel==0&&t.A.scope==Env::ScopeState::Closed,"6b 回呼內關閉，同輪後續通知不遞送",out);}
        {Session t(1000);t.A.service_lease_held=true;++t.service.leases;const int l0=t.service.leases;t.A.outer_traversals=1;t.A.close(5);run_until_idle(t);t.step();
         check(t.A.scope==Env::ScopeState::Closing&&t.service.leases==l0,"6c 外層遍歷仍在：保持收尾中",out);t.A.outer_traversals=0;t.step();
         check(t.A.scope==Env::ScopeState::Closed&&t.service.leases==l0-1,"6c 遍歷結束才關閉並釋放租用",out);}
    }
    // ---- v4: capacity ----
    {   Session s(1000,4);out<<"\n== 情境7：普通容量（A 容量 4）==\n";auto& A=s.A;A.add_target(3);
        A.set('m',1);for(int i=0;i<3;++i)A.post_event(3);
        check(A.ordinary_used()==4&&A.ordinary_free()==0,"佔滿 4 個普通容量",out);
        const EnvSnapshot before=snap(A);
        const bool ev=A.post_event(3),newset=A.set('n',1);
        check(!ev&&!newset&&snap(A)==before&&!A.value.count('n'),"滿載時事件與需要新通知的設定都未受理，值、佇列、終態、租用不變",out);
        check(A.set('m',2)&&A.value['m']==2&&A.ordinary_used()==4,"已有下一輪通知的設定可合併受理，值立即更新、不佔新容量",out);
        s.budget={1000,2};s.step();
        check(A.ordinary_used()==(A.round.size()-A.cursor)+A.next.size()&&A.ordinary_used()==3,"單步後（輪開始＋1 項）：本輪剩餘仍計入佔用 3",out);
        check(A.post_event(3)&&!A.post_event(3),"釋出 1 個容量後恢復受理 1 項",out);
        Session one(1000,1);one.A.add_target(3);check(one.A.post_event(3)&&!one.A.post_event(3),"容量 1：受理 1、第 2 項未受理",out);
        Session zero(1000,0);zero.A.add_target(3);check(!zero.A.post_event(3)&&!zero.A.set('k',1)&&!zero.A.value.count('k'),"容量 0（明確政策）：一律未受理",out);
        check(A.c.offers==A.c.accepted+A.c.not_accepted&&accounted(A),"提供數 = 受理 + 未受理；事件計帳一致",out);
    }
    {   Session s(1000,16);out<<"\n== 情境7b：持續超載 600 步（生產者每步 5 項，A 容量 16，額度 3 項）==\n";auto& A=s.A;auto& B=s.B;A.add_target(3);s.budget={1000,3};
        A.verbose=false;B.verbose=false;bool ok_each=true;
        for(int step=0;step<600;++step){for(int i=0;i<5;++i)A.post_event(3);if(step%3==0)B.post_event(9);s.step();
            if(A.ordinary_used()>A.ordinary_cap||A.round.size()>A.ordinary_cap||A.next.size()>A.ordinary_cap||!accounted(A))ok_each=false;}
        const int pending_at_stop=(int)A.pending_events();run_until_idle(s);
        out<<"A 提供="<<A.c.offers<<" 受理="<<A.c.accepted<<" 未受理="<<A.c.not_accepted<<" 執行="<<A.c.events_executed<<" 停止生產時待處理="<<pending_at_stop
           <<" 峰值 邏輯佔用="<<A.peak_used<<" 本輪/下一輪記錄="<<A.peak_round<<"/"<<A.peak_next<<" 實際保留記錄（本輪含已消耗前段＋下一輪）="<<A.peak_retained
           <<" 兩個 vector 配置容量合計="<<A.peak_capacity<<"  B 執行="<<B.c.events_executed<<"\n";
        check(ok_each&&A.peak_used<=16&&A.peak_round<=16&&A.peak_next<=16,"邏輯佔用、本輪與下一輪各自都不超過容量 16",out);
        check(A.peak_retained<=32&&A.peak_capacity<=32,"實際保留的 Work 記錄與配置容量都不超過 2×容量（32），長時間執行沒有成長；不是整個程式的記憶體保證",out);
        check(A.c.offers==A.c.accepted+A.c.not_accepted&&A.c.not_accepted>0,"提供 = 受理 + 未受理，且確實發生過未受理",out);
        check(accounted(A)&&A.pending_events()==0&&A.c.events_accepted==A.c.events_executed,"停止生產後處理完：受理 = 執行，剩餘 0",out);
        check(B.c.events_executed==200,"超載期間 B 的 200 個事件全部執行",out);
    }
    // ---- v4: terminal reservation ----
    {   Session s(100000,4,3);out<<"\n== 情境8：終態預留（A 普通 4、終態 3）==\n";auto& A=s.A;A.add_target(3);A.add_target(5);
        for(int i=0;i<4;++i)A.post_event(3);
        Token t1=A.start_op(3),t2=A.start_op(3),t3=A.start_op(5);const int leases=s.service.leases;
        Token t4=A.start_op(3);
        check(t1.valid()&&t2.valid()&&t3.valid()&&!t4.valid()&&s.service.leases==leases&&A.c.ops_not_started==1,"第 4 個操作因終態槽位滿而不啟動，不取得租用",out);
        check(A.ordinary_free()==0&&A.ledger->publish(t1,{11,false}),"普通容量全滿時，已預留的終態結果仍可發布",out);
        A.request_remove(5);  // target 5 will be removed/reused before t3 settles
        s.step();s.step();s.step();
        check(A.c.completions_settled==1&&A.ledger->used()==2,"普通全滿下仍結算 t1 並歸還槽位",out);
        const size_t ready0=A.ledger->ready();const int rej0=A.ledger->rejected_publish;
        const bool unknown=A.ledger->publish({A.ledger->id,99,1},{0,false}),stale=A.ledger->publish(t1,{0,false});
        check(!unknown&&!stale&&A.ledger->ready()==ready0&&A.ledger->rejected_publish==rej0+2,"未知槽位、舊世代 token 都被拒絕，不增加儲存",out);
        Token t5=A.start_op(3);check(t5.valid()&&t5.slot==t1.slot&&t5.gen!=t1.gen&&!A.ledger->publish(t1,{0,false}),"歸還的槽位可重用，舊 token（舊世代）被拒絕",out);
        run_until_idle(s);A.reuse_target(5);
        check(A.ledger->publish(t3,{55,false}),"目標 5 已移除並重建後，t3 仍可發布終態",out);
        A.close(10);
        check(A.ledger->publish(t2,{22,false})&&!A.ledger->publish(t2,{22,false}),"收尾中仍接受 t2；重複發布被拒絕",out);
        A.ledger->publish(t5,{0,true});run_until_idle(s);dump(s,out);
        check(A.targets[5].value==0&&A.c.completions_rejected_stale_target>=1,"晚到的 t3 不回寫到重建的目標 5",out);
        check(A.ledger->used()==0&&A.ledger->released==4&&s.service.leases==1&&A.scope==Env::ScopeState::Closed,"4 個終態各歸還一次；租用只剩 B；A 已關閉",out);
        // Many reserve/publish/settle cycles: storage is the fixed slot array.
        Session m(1000,4,3);m.A.verbose=false;m.A.add_target(3);bool cyc=true;
        for(int i=0;i<3000;++i){Token t=m.A.start_op(3);if(!t.valid()||!m.A.ledger->publish(t,{i,false})){cyc=false;break;}run_until_idle(m);}
        check(cyc&&m.A.ledger->used()==0&&m.A.ledger->released==3000&&m.A.ledger->slots.size()==3&&m.A.ledger->ring.size()==3&&m.service.leases==1,
              "3000 次預留→發布→結算：槽位與環形佇列固定 3 格，全部歸還",out);
    }
    // ---- v4: mixed sustained flow ----
    for(int count:{1,2,3}){
        Session s(1000,32,4);out<<"\n== 情境9：持續混合流量（額度 "<<count<<" 項，收尾配額 2）==\n";auto& A=s.A;auto& B=s.B;A.verbose=false;B.verbose=false;
        A.add_target(3);s.budget={1000,count};
        int b_wait=0,max_b_wait=0,a_delivery_while_ready=0,a_step_gap=0,max_a_step_gap=0;int rejected_bogus=0;
        auto deliveries=[&]{return A.c.events_executed+A.c.events_rejected_delivery;};
        for(int step=0;step<2000;++step){
            A.post_event(3);A.post_event(3);if(step%2==0)B.post_event(9);
            Token t=A.start_op(3);if(t.valid())A.ledger->publish(t,{step,false});
            for(int i=0;i<5;++i)if(!A.ledger->publish({A.ledger->id,(std::uint32_t)(step%7),12345},{0,false}))++rejected_bogus;
            const bool b_waiting=B.has_work(),a_waiting=A.pending_events()>0;const int a_del=deliveries();const bool ready=A.ledger->ready()>0;
            Env* e=s.step();
            if(e==&B)b_wait=0;else if(b_waiting)max_b_wait=std::max(max_b_wait,++b_wait);
            if(deliveries()>a_del){a_step_gap=0;if(ready)++a_delivery_while_ready;}else if(a_waiting)max_a_step_gap=std::max(max_a_step_gap,++a_step_gap);
        }
        // Measured during the producer phase, before any drain.
        const int a_delivered=deliveries(),b_executed=B.c.events_executed;
        run_until_idle(s);
        out<<"生產期間（未 drain）：A 實際事件遞送="<<a_delivered<<" B 實際執行="<<b_executed<<" 有結果待結算時 A 仍遞送事件的步數="<<a_delivery_while_ready<<"\n"
           <<"  A 事件遞送之間最大間隔：工作單位="<<A.max_delivery_gap<<"、host 步="<<max_a_step_gap
           <<"；內部普通單位（含提交／開新輪）最大等待="<<A.max_ordinary_gap<<"；B 最大等待 host 步="<<max_b_wait<<"；拒絕偽造發布="<<rejected_bogus<<"\n";
        check(A.max_ordinary_gap<=A.cleanup_quota,"收尾連續處理不超過配額 2（跨片保存的連續數；觀察值）",out);
        check(a_delivered>0&&a_delivery_while_ready>0&&b_executed>0,"生產期間 A 有實際事件遞送（含結果待結算時），B 也有實際執行",out);
        check(max_b_wait<=1,"兩個環境輪替：B 有工作時最多等 1 個 host 步",out);
        check(rejected_bogus==10000&&A.ledger->ready()==0&&accounted(A)&&A.pending_events()==0,"偽造發布全部拒絕、不佔儲存；處理完後計帳一致",out);
    }
    // ---- v4: close under load ----
    {   Session s(100000,4,3);out<<"\n== 情境10：普通滿載＋多個預留操作時關閉 ==\n";auto& A=s.A;auto& B=s.B;A.add_target(3);
        for(int i=0;i<4;++i)A.post_event(3);Token t1=A.start_op(3),t2=A.start_op(3),t3=A.start_op(3);
        check(s.service.leases==4,"租用：B 1 + 3 個預留操作",out);
        A.close(5);
        const EnvSnapshot before=snap(A);
        check(!A.post_event(3)&&!A.start_op(3).valid()&&!A.set('x',1)&&snap(A)==before,"收尾中普通受理與啟動操作都被拒絕，狀態不變",out);
        s.step(6);s.step();s.step();
        check(A.scope==Env::ScopeState::Closing&&A.overdue&&A.ledger->used()==3&&s.service.leases==4,"逾期但仍收尾中；3 個終態槽位與租用保留",out);
        check(A.ledger->publish(t1,{1,true})&&A.ledger->publish(t2,{2,false}),"收尾中接受有效完成",out);
        run_until_idle(s);
        check(A.scope==Env::ScopeState::Closing&&A.ledger->used()==1&&s.service.leases==2,"t3 的 worker 尚未發布：仍收尾中，只保留 1 槽 1 租用",out);
        check(!A.ledger->publish(t1,{1,true}),"關閉後重複發布 t1 無害地被拒絕",out);
        check(A.ledger->publish(t3,{3,false}),"t3 發布",out);run_until_idle(s);dump(s,out);
        check(A.scope==Env::ScopeState::Closed&&A.ledger->used()==0&&A.ledger->released==3&&s.service.leases==1,"全部結算、各歸還一次後才關閉；B 仍持有服務",out);
        check(accounted(A)&&A.c.events_rejected_delivery==4,"關閉前已受理的 4 個事件都有處理結果（拒絕遞送），沒有丟棄",out);
        B.post_event(9);run_until_idle(s);check(B.c.events_executed==1,"B 繼續執行",out);
    }
    // ---- v5: token bound to its ledger instance ----
    {   Session s(1000,8,2,8,2);out<<"\n== 情境11：跨帳本 token 不能互用 ==\n";s.A.add_target(3);s.B.add_target(4);
        Token ta=s.A.start_op(3),tb=s.B.start_op(4);
        out<<"A token 槽位"<<ta.slot<<"世代"<<ta.gen<<"；B token 槽位"<<tb.slot<<"世代"<<tb.gen<<"\n";
        const size_t b_ready=s.B.ledger->ready(),b_used=s.B.ledger->used();const int b_pub=s.B.ledger->published;
        const bool wrong=s.B.ledger->publish(ta,{1,false});
        check(!wrong&&s.B.ledger->ready()==b_ready&&s.B.ledger->used()==b_used&&s.B.ledger->published==b_pub,"A 的 token 發布到 B 的帳本被拒絕，B 不變",out);
        s.A.close(1);s.A.ledger->request_cancel_all();
        check(!s.B.ledger->cancel_requested(ta)&&s.A.ledger->cancel_requested(ta),"cancel_requested 不接受外來 token",out);
        TerminalLedger fresh(2);Token tf{};fresh.reserve(3,1,tf);
        check(!fresh.publish(ta,{0,false})&&fresh.ready()==0,"新建立的帳本不接受舊帳本的 token",out);
        check(s.A.ledger->publish(ta,{1,false})&&s.B.ledger->publish(tb,{2,false}),"各自的 token 在自己的帳本仍有效",out);
        run_until_idle(s);
    }
    // ---- v5: target generation alone (scope stays Running) ----
    {   Session s(100000,8,2);out<<"\n== 情境12：執行中只靠目標世代拒絕舊結果 ==\n";auto& A=s.A;A.add_target(5);
        const int leases0=s.service.leases;Token t=A.start_op(5);
        A.request_remove(5);run_until_idle(s);A.reuse_target(5);
        check(A.targets[5].gen==2&&A.targets[5].s==Env::TargetState::Live,"目標 5 已移除並以世代 2 重建",out);
        check(A.ledger->publish(t,{77,false}),"舊操作的 token 仍有效（槽位預留仍在）",out);
        run_until_idle(s);dump(s,out);
        check(A.scope==Env::ScopeState::Running&&A.targets[5].value==0&&A.c.completions_rejected_stale_target==1,"範圍仍在執行中，結果因世代不符被拒絕，新目標值不變",out);
        check(A.ledger->used()==0&&A.ledger->released==1&&s.service.leases==leases0,"槽位與服務租用各歸還一次",out);
        // start_op on invalid targets, terminal capacity 0 and 1.
        A.add_target(6);A.request_remove(6);
        const bool missing=!A.start_op(77).valid(),pending=!A.start_op(6).valid();run_until_idle(s);const bool removed=!A.start_op(6).valid();
        check(missing&&pending&&removed&&A.ledger->used()==0&&s.service.leases==leases0,"start_op 對不存在／待移除／已移除目標都拒絕，不預留、不取得租用",out);
        Session z(1000,8,0);z.A.add_target(3);check(!z.A.start_op(3).valid()&&z.service.leases==1,"終態容量 0：任何操作都不啟動",out);
        Session o(1000,8,1);o.A.add_target(3);Token a=o.A.start_op(3);check(a.valid()&&!o.A.start_op(3).valid()&&o.service.leases==2,"終態容量 1：第 1 個啟動、第 2 個不啟動",out);
    }
    out<<"\nscenarios "<<(failures?"FAIL":"PASS")<<" failures="<<failures<<'\n';
}

// ---------- benchmark (real steady_clock, native threads) ----------
static void spin_us(double us){auto end=std::chrono::steady_clock::now()+std::chrono::duration<double,std::micro>(us);while(std::chrono::steady_clock::now()<end){}}
static double pct(std::vector<double> v,double p){std::sort(v.begin(),v.end());return v[std::min(v.size()-1,static_cast<size_t>(p*(v.size()-1)+0.5))];}
static std::string stats(const std::vector<double>& v){std::ostringstream o;o<<std::fixed<<std::setprecision(3)<<pct(v,.5)<<'/'<<pct(v,.95)<<'/'<<*std::max_element(v.begin(),v.end());return o.str();}

static void benchmark(std::ostream& summary,std::ostream& csv){
    summary<<"v5. steady_clock CPU wall time. Work cost = controlled busy-wait (synthetic). Trace formatting disabled in slices; files written after runs.\n"
             "Slice time covers: boundary checks, completion settlement, notice/event dispatch (incl. rejected delivery), commit, round promotion.\n"
             "Host step time additionally covers close-state ticks. Producer offers and publications happen between steps, outside both timers.\n";
    csv<<"case,budget_ms,budget_count,cost_us,slice,slice_ms,host_step_ms,executed,stop\n";
    struct Case{const char* name;double ms;int count;double cost;bool overload;};
    for(auto k:std::vector<Case>{{"batch",0.5,4,50,false},{"batch",0.5,16,50,false},{"batch",2,16,50,false},{"batch_zero_cost",2,16,0,false},
                                 {"sustained_overload_mixed",0.5,8,50,true},{"sustained_overload_mixed",2,16,50,true}}){
        SteadyClock clock;Trace trace(1);Service service;Env A(1,"A",clock,trace,service,k.overload?64:4096,4),B(2,"B",clock,trace,service,4096,4);Host host;host.envs={&A,&B};
        A.verbose=B.verbose=false;A.add_target(1);B.add_target(1);double cost=k.cost;
        A.on_event=[&](Env&,int){spin_us(cost);};B.on_event=[&](Env&,int){spin_us(cost);};
        if(!k.overload)for(int i=0;i<2000;++i){A.post_event(1);B.post_event(1);}
        std::vector<double> el,host_el,ex;std::map<Stop,int> stops;int slices=0,steps=0;
        while(slices<40&&steps<100000){
            if(k.overload){for(int i=0;i<24;++i)A.post_event(1);B.post_event(1);Token t=A.start_op(1);if(t.valid())A.ledger->publish(t,{steps,false});}
            SliceResult r{};auto t0=std::chrono::steady_clock::now();Env* e=host.step({k.ms,k.count},&r);++steps;
            double hs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();if(!e)break;
            if(e==&A){++slices;el.push_back(r.elapsed);host_el.push_back(hs);ex.push_back(r.executed);++stops[r.stop];
                csv<<k.name<<','<<k.ms<<','<<k.count<<','<<cost<<','<<slices<<','<<r.elapsed<<','<<hs<<','<<r.executed<<','<<stop_name(r.stop)<<'\n';}
        }
        const int pending_after=(int)A.pending_events();
        for(int i=0;i<1000000&&!host.idle();++i)host.step({k.ms,k.count});  // drain outside measurement
        const bool ok=slices==40&&accounted(A)&&accounted(B)&&A.pending_events()==0&&B.pending_events()==0&&A.ledger->used()==0&&service.leases==0
            &&A.c.offers==A.c.accepted+A.c.not_accepted&&A.peak_used<=A.ordinary_cap;
        if(!ok)++failures;
        summary<<std::fixed<<std::setprecision(3)<<"\n"<<k.name<<" budget="<<k.ms<<"ms/"<<k.count<<" cost="<<cost<<"us A_cap="<<A.ordinary_cap<<" sampled_A_slices="<<slices
               <<"\n  slice ms p50/p95/max: "<<stats(el)<<"   host step ms p50/p95/max: "<<stats(host_el)
               <<"\n  items per slice p50/p95/max: "<<stats(ex)<<"  stop: count="<<stops[Stop::Count]<<" time="<<stops[Stop::Time]
               <<"\n  A offers="<<A.c.offers<<" accepted="<<A.c.accepted<<" not_accepted="<<A.c.not_accepted<<" pending_after_sample="<<pending_after
               <<" peak_used="<<A.peak_used<<" settled="<<A.c.completions_settled<<" max_ordinary_gap="<<A.max_ordinary_gap
               <<"; after drain A executed="<<A.c.events_executed<<" pending="<<A.pending_events()<<" B executed="<<B.c.events_executed<<(ok?"  [PASS accounting]":"  [FAIL accounting]")<<'\n';
    }
    {   SteadyClock clock;Trace trace(1);Service service;Env A(1,"A",clock,trace,service,4096,4);Host host;host.envs={&A};A.verbose=false;A.add_target(1);
        bool long_done=false;A.on_event=[&](Env&,int){if(!long_done){long_done=true;spin_us(5000);}else spin_us(50);};
        for(int i=0;i<40;++i)A.post_event(1);auto r1=A.slice({0.5,16});auto r2=A.slice({0.5,16});
        for(int i=0;i<1000&&!host.idle();++i)host.step({0.5,16});
        const bool ok=r1.elapsed>=5.0&&r1.stop==Stop::Time&&accounted(A)&&A.pending_events()==0;if(!ok)++failures;
        summary<<"\nlong_callback_demo (separate, not a cohort): slice1 "<<r1.elapsed<<"ms executed="<<r1.executed<<"; slice2 "<<r2.elapsed<<"ms; "<<(ok?"PASS":"FAIL")<<'\n';
    }
}

// Native-thread close: bounded workers hold only a ledger pointer + token (no UI pointer) and publish once.
static void native_close(std::ostream& out){
    SteadyClock clock;Trace trace(100000);Service service;Env A(1,"環境A",clock,trace,service,8,4),B(2,"環境B",clock,trace,service,4096,4);Host host;host.envs={&A,&B};
    B.service_lease_held=true;++service.leases;A.add_target(1);B.add_target(9);
    struct Worker{std::thread t;std::shared_ptr<std::atomic<bool>> finished=std::make_shared<std::atomic<bool>>(false);};
    auto launch=[&](Token tok,bool cooperative,int steps){Worker w;auto ledger=A.ledger;auto f=w.finished;
        w.t=std::thread([ledger,tok,f,cooperative,steps]{bool seen=false;for(int i=0;i<steps;++i){if(ledger->cancel_requested(tok)){seen=true;if(cooperative)break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            ledger->publish(tok,{7,seen});f->store(true);});return w;};
    Token t1=A.start_op(1),t2=A.start_op(1);
    std::vector<Worker> workers;workers.push_back(launch(t1,false,60));workers.push_back(launch(t2,true,60));
    out<<"worker1：故意忽略取消，迴圈 60 次 sleep_for(1ms)；worker2：合作式。兩者只持有帳本指標與 token。\n";
    const double start=clock.ms();bool closed=false,watchdog=false;int b_at_close=0,b_while_closing=0,leases_while_overdue=-1;double t_close=0,t_overdue=0,t_closed=0;
    auto all_finished=[&]{for(auto& w:workers)if(!w.finished->load())return false;return true;};
    while(!(A.scope==Env::ScopeState::Closed&&all_finished())){
        if(!closed&&clock.ms()-start>=5){for(int i=0;i<8;++i)A.post_event(1);A.close(20);closed=true;t_close=clock.ms()-start;b_at_close=B.c.events_executed;}
        B.post_event(9);host.step({1,8});
        if(closed&&A.scope==Env::ScopeState::Closing)b_while_closing=B.c.events_executed-b_at_close;
        if(A.overdue&&leases_while_overdue<0){leases_while_overdue=service.leases;t_overdue=clock.ms()-start;}
        if(A.scope==Env::ScopeState::Closed&&t_closed==0)t_closed=clock.ms()-start;
        if(!watchdog&&clock.ms()-start>5000){watchdog=true;out<<"診斷：5 秒仍未完成（只回報，不強制釋放；繼續排程）\n";}
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    for(auto& w:workers)w.t.join();
    while(!host.idle())host.step({1,8});
    out<<std::fixed<<std::setprecision(1)<<"觀察時間：關閉 "<<t_close<<"ms、逾期 "<<t_overdue<<"ms、已關閉 "<<t_closed<<"ms（實際觀察值）\n"
       <<"結算="<<A.c.completions_settled<<" 拒絕回寫="<<A.c.completions_rejected_stale_target<<" 目標值="<<A.targets[1].value<<" A 收尾期間 B 執行事件="<<b_while_closing
       <<" 逾期時租用="<<leases_while_overdue<<" 結束租用="<<service.leases<<" 槽位歸還="<<A.ledger->released<<'\n';
    const bool ok=A.scope==Env::ScopeState::Closed&&A.c.completions_settled==2&&A.c.completions_rejected_stale_target==2&&A.targets[1].value==0&&b_while_closing>0
        &&leases_while_overdue>=2&&service.leases==1&&A.ledger->released==2&&accounted(A)&&A.pending_events()==0;
    if(!ok)++failures;out<<(ok?"PASS":"FAIL")<<" native close（只在 finished 後 join）\n";
}

// ---------- interactive Win32 ----------
static std::unique_ptr<Session> live;static int live_kind=1;static HWND list_box,status_box;
static void reset_live(){
    if(live_kind==5)live=std::make_unique<Session>(400,4,3);else if(live_kind==6)live=std::make_unique<Session>(400,8,3);
    else if(live_kind==7)live=std::make_unique<Session>(400,4,3);else live=std::make_unique<Session>(400);
    if(live_kind==1)build_s1(*live);else if(live_kind==5)build_overload(*live);else if(live_kind==6)build_mixed(*live);else build_closing(*live);
}
static std::wstring widen(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n);return w;}
static const char* scope_name(Env::ScopeState s){return s==Env::ScopeState::Running?"執行中":s==Env::ScopeState::Closing?"收尾中":"已關閉";}
static void refresh(){
    auto& s=*live;auto& A=s.A;std::ostringstream o;o.setf(std::ios::fixed);o.precision(1);
    o<<s.name<<"  模擬時間 "<<s.clock.t<<"ms  額度 "<<s.budget.ms<<"ms/"<<s.budget.count<<"項  範圍："<<scope_name(A.scope)<<(A.overdue?"（逾期）":"")<<"\r\n"
     <<"A 普通容量 已用 "<<A.ordinary_used()<<" / 空 "<<A.ordinary_free()<<"（上限 "<<A.ordinary_cap<<"）  終態槽位 已用 "<<A.outstanding()<<" / 空 "<<A.ledger->cap()-A.outstanding()<<"  待結算 "<<A.ledger->ready()<<"\r\n"
     <<"輪"<<A.round_no<<" 游標 "<<A.cursor<<"/"<<A.round.size()<<" 下一輪 "<<A.next.size()<<"  收尾連續 "<<A.cleanup_streak<<"/"<<A.cleanup_quota<<"\r\n"
     <<"提供 "<<A.c.offers<<" 受理 "<<A.c.accepted<<" 未受理 "<<A.c.not_accepted<<"  事件 執行 "<<A.c.events_executed<<" 拒絕遞送 "<<A.c.events_rejected_delivery<<" 待處理 "<<A.pending_events()<<"\r\n"
     <<"操作 啟動 "<<A.c.ops_started<<" 未啟動 "<<A.c.ops_not_started<<" 結算 "<<A.c.completions_settled<<" 拒絕發布 "<<A.ledger->rejected_publish
     <<"  共用服務租用 "<<s.service.leases<<"\r\n"
     <<"B 普通容量 已用 "<<s.B.ordinary_used()<<"/"<<s.B.ordinary_cap<<"  B 終態槽位 已用 "<<s.B.outstanding()<<"/"<<s.B.ledger->cap()<<"  B 事件執行 "<<s.B.c.events_executed;
    SetWindowTextW(status_box,widen(o.str()).c_str());
    SendMessageW(list_box,LB_RESETCONTENT,0,0);
    for(auto& l:s.trace.lines)SendMessageW(list_box,LB_ADDSTRING,0,(LPARAM)widen(l).c_str());
    SendMessageW(list_box,LB_SETTOPINDEX,s.trace.lines.empty()?0:s.trace.lines.size()-1,0);
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_COMMAND){
        auto& s=*live;
        switch(LOWORD(w)){
        case 1:case 5:case 6:case 7:live_kind=LOWORD(w);reset_live();break;
        case 10:s.step(1);break;
        case 11:s.A.close(3);break;
        case 12:{Token t;if(s.A.ledger->first_reserved(t)){s.A.ledger->publish(t,{99,false});s.trace.add("（使用者發布槽位"+std::to_string(t.slot)+"的終態結果）");}else s.trace.add("（沒有已預留未發布的槽位）");break;}
        case 13:if(!s.A.start_op(3).valid())s.trace.add("（操作未啟動）");break;
        case 14:reset_live();break;
        case 15:s.B.post_event(9);break;
        case 16:{int ok=0;for(int i=0;i<8;++i)ok+=s.A.post_event(3);s.trace.add("（加負載：提供 8 項，受理 "+std::to_string(ok)+"）");break;}
        case 17:{const int r=s.A.ledger->rejected_publish;s.A.ledger->publish({0,0,1},{0,false});/* ledger id 0 is never issued (ids start at 1): always foreign */s.trace.add("（偽造發布：拒絕數 "+std::to_string(r)+"→"+std::to_string(s.A.ledger->rejected_publish)+"，不佔儲存）");break;}
        }
        refresh();return 0;
    }
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static int interactive(){
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"UpdateSchedulerPrototype";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    HWND win=CreateWindowExW(0,wc.lpszClassName,L"可丟棄原型 v5：更新排程（模擬時鐘）",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1100,800,nullptr,nullptr,wc.hInstance,nullptr);
    // Three rows, all inside the client area.
    const wchar_t* labels[]={L"情境1",L"超載",L"混合流量",L"滿載中關閉",L"重設",
                             L"單步一片",L"加負載(A 8項)",L"B送事件",L"A啟動操作",
                             L"發布一個結果",L"偽造發布",L"關閉A"};
    int ids[]={1,5,6,7,14, 10,16,15,13, 12,17,11};int row_len[]={5,4,3};
    for(int i=0,r=0,c=0;i<12;++i){CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,10+c*170,10+r*36,162,30,win,(HMENU)(INT_PTR)ids[i],wc.hInstance,nullptr);if(++c==row_len[r]){c=0;++r;}}
    status_box=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,10,122,1060,116,win,nullptr,wc.hInstance,nullptr);
    list_box=CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_BORDER|LBS_NOINTEGRALHEIGHT,10,244,1060,500,win,nullptr,wc.hInstance,nullptr);
    reset_live();refresh();
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    live.reset();return 0;
}

int main(int argc,char** argv){
    SetConsoleOutputCP(CP_UTF8);
    std::string mode=argc>1?argv[1]:"";
    try{
        if(mode=="--scenarios"){std::ofstream f("scenarios-v5.log",std::ios::binary);scenarios(f);f.close();std::cout<<"scenarios failures="<<failures<<" -> scenarios-v5.log\n";return failures?1:0;}
        if(mode=="--benchmark"){std::ofstream s("benchmark-v5-summary.log",std::ios::binary),c("benchmark-v5-slices.csv",std::ios::binary);benchmark(s,c);
            s<<"\n== native thread close ==\n";native_close(s);s<<"\nbenchmark failures="<<failures<<'\n';
            std::cout<<"benchmark failures="<<failures<<" -> benchmark-v5-summary.log / benchmark-v5-slices.csv\n";return failures?1:0;}
        if(mode=="--interactive")return interactive();
        std::cerr<<"use --scenarios | --benchmark | --interactive\n";return 2;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
