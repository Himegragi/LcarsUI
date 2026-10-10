// THROWAWAY safe-handoff prototype v4 shell: --scenarios (self-checking) and --interactive (Win32). Logic in handoff_model.hpp.
#include "handoff_model.hpp"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>

using namespace handoff;
using AR=ActionResult;

static int failures=0,inv_checks=0;
static void check(bool ok,const std::string& what,std::ostream& out){out<<(ok?"PASS ":"FAIL ")<<what<<'\n';if(!ok)++failures;}
static void dump(World& w,std::ostream& out){for(auto& l:w.log)out<<"  "<<l<<'\n';w.log.clear();}
static auto thrower=[]{throw std::runtime_error("extension callback threw");};
static FaultSpec unknown_shared(const std::string& svc){FaultSpec s;s.service=svc;s.reason="shared state of "+svc+" unknown";return s;}
// Capacity equation (notice_used == running instances + unreleased handoff credits, <= cap), checked after transitions.
static void I(World& w,std::ostream& out){++inv_checks;if(!w.invariant()){++failures;out<<"FAIL 容量方程式不成立 notice_used="<<w.notice_used<<" running="<<w.running()<<" held="<<w.held_credits()<<'\n';}}
static size_t open_count(World& w){size_t n=0;for(auto& h:w.handoffs)n+=h.id!=0;return n;}
static Handoff* first(World& w){for(auto& h:w.handoffs)if(h.id)return &h;return nullptr;}
// State snapshot for "refusal leaves state unchanged" checks (independent of refusal counters).
static std::string snap(World& w){std::ostringstream o;
    o<<w.notice_used<<'|'<<w.held_credits()<<'|'<<w.c.released<<'|'<<w.c.confirmed<<'|'<<w.instances.size()<<'|'<<w.ledger->used()<<'|'<<w.active_receiver<<'|'<<w.receivers.size();
    for(auto& h:w.handoffs){o<<'['<<h.id<<','<<h.phase<<','<<h.credits<<','<<h.current<<','<<h.req.s<<','<<h.req.id;for(auto& a:h.asg)o<<','<<a.id<<a.authority<<a.work<<a.callback_active<<a.route_failed;o<<']';}
    for(auto& [e,i]:w.current)o<<e<<i;for(auto& i:w.instances)o<<(int)i.state<<i.value;return o.str();}
static Callback keep(int& calls){return [&calls](World&,const Notification&,const AckToken&){++calls;return HostReply::KeepProcess;};}
static Callback noack(int& calls,std::vector<AckToken>& toks){return [&calls,&toks](World&,const Notification&,const AckToken& t){++calls;toks.push_back(t);return HostReply::KeepProcess;};}
static Callback ack_in(int& calls){return [&calls](World& w,const Notification&,const AckToken& t){++calls;w.confirm(t);return HostReply::KeepProcess;};}
static std::uint64_t setup(World& w,Callback cb,bool post_safe=true){auto r=w.add_receiver("host",std::move(cb),post_safe);w.set_active_receiver(r);return r;}

// Rig: A faulted and delivered to r1, whose callback started one tracked work item and did not confirm.
struct Rig{World w;std::uint64_t r1=0,r2=0,h=0,req=0;AckToken t1{},t2{};std::optional<WorkToken> wk;int calls1=0,calls2=0;};
static std::unique_ptr<Rig> rig(bool request=true){auto p=std::make_unique<Rig>();Rig& g=*p;
    g.r1=setup(g.w,[&g](World& w,const Notification&,const AckToken& t){++g.calls1;g.t1=t;g.wk=w.begin_work(t);return HostReply::KeepProcess;});
    g.r2=g.w.add_receiver("host",[&g](World&,const Notification&,const AckToken& t){++g.calls2;g.t2=t;return HostReply::KeepProcess;});
    g.w.add_env("A",{"svcA"},false);g.w.dispatch("A",thrower,unknown_shared("svcA"));g.w.deliver();g.h=first(g.w)->id;
    if(request){auto r=g.w.reassign(g.h,g.r2);g.req=r?*r:0;}return p;}

static void scenarios(std::ostream& out){
    out<<"安全交接原型 v5 情境（v4 語意＋審查修正 29–33）（模擬；交接路徑安全、收尾安全、確認後回呼失敗是否安全，都是 fixture 宣告，不是偵測結果）。\n";
    out<<"v4 語意：需要主動接收者才能啟用；只有明確且有效的確認才算接受；額度要在確認＋回呼安全結束＋安全釋放後才歸還。\n";
    // ---- 1..21: v3 scenarios adapted to v4 semantics (receiver required, explicit acknowledgment) ----
    {   out<<"\n== 1 基本安全交接：A 故障，B 獨立 ==\n";World w;int calls=0;std::vector<std::string> seen;
        setup(w,[&](World&,const Notification& n,const AckToken&){++calls;seen=n.envs;return HostReply::KeepProcess;});
        w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);
        auto r=w.dispatch("A",thrower,unknown_shared("svcA"));I(w,out);
        check(r==AR::Failed&&w.cur("A")->state==InstanceState::Unavailable,"A 回呼例外被攔截 → A 不可用",out);
        check(w.set_value("A",1)==AR::RefusedUnavailable&&w.rebuild_target("A")==AR::RefusedUnavailable,"A 的普通路徑立即拒絕",out);
        Token t;check(w.start_op("A",t)==AR::RefusedUnavailable&&w.ledger->used()==0,"A 不能啟動新工作，不預留槽位",out);
        check(w.set_value("B",5)==AR::Ok&&w.cur("B")->value==5,"B 獨立，繼續運作",out);
        check(calls==0&&open_count(w)==1,"交接只登記，回呼中不呼叫宿主",out);
        w.deliver();I(w,out);dump(w,out);
        check(calls==1&&seen==std::vector<std::string>{"A"}&&!w.process_exit_requested,"宿主收到一則交接（只含 A），選擇保留程序",out);
        check(w.cur("A")->state==InstanceState::Unavailable&&first(w)->phase==Handoff::AwaitingAck,"回呼返回不等於確認，也不等於恢復",out);
        w.dispatch("A",thrower,unknown_shared("svcA"));w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();
        check(calls==1&&w.c.repeated_fault_attempts==2&&open_count(w)==1,"同一故障世代的重複嘗試不產生交接風暴",out);
    }
    {   out<<"\n== 2 共享服務故障：A＋B 都不可用 ==\n";World w;int calls=0;Notification got{};
        setup(w,[&](World&,const Notification& n,const AckToken&){++calls;got=n;return HostReply::KeepProcess;});
        w.add_env("A",{"shared"},false);w.add_env("B",{"shared"},false);w.add_env("C",{"own"},true);
        w.dispatch("A",thrower,unknown_shared("shared"));w.deliver();I(w,out);dump(w,out);
        check(w.cur("A")->state==InstanceState::Unavailable&&w.cur("B")->state==InstanceState::Unavailable,"共用 shared 的 A、B 都不可用",out);
        check(w.cur("C")->state==InstanceState::Running&&w.set_value("C",1)==AR::Ok,"獨立的 C 繼續",out);
        check(calls==1&&got.envs.size()==2&&first(w)->credits==2,"一則交接涵蓋 A、B，持有 2 個額度",out);
        World u;int uc=0;setup(u,keep(uc));u.add_env("A",{"x"},false);u.add_env("B",{"y"},false);u.add_env("C",{"z"},true);
        FaultSpec unk;unk.dependency_known=false;u.dispatch("A",thrower,unk);I(u,out);
        check(u.cur("B")->state==InstanceState::Unavailable&&u.cur("C")->state==InstanceState::Running,"相依未知：擴大到所有未宣告隔離的環境（B），宣告隔離的 C 繼續",out);
    }
    {   out<<"\n== 3 巢狀遍歷中故障：交接送出與收尾延後 ==\n";World w;int calls=0;setup(w,keep(calls));
        w.add_env("A",{"svcA"},false);
        w.traversals=1;w.dispatch("A",thrower,unknown_shared("svcA"));
        check(w.cur("A")->state==InstanceState::Unavailable&&w.set_value("A",1)==AR::RefusedUnavailable,"遍歷中：A 立即不可用、拒絕操作",out);
        check(w.deliver()==0&&calls==0&&w.c.deferred_delivery==1,"遍歷仍在：延後，不在遍歷中呼叫宿主",out);
        w.traversals=0;w.deliver();I(w,out);dump(w,out);check(calls==1,"遍歷結束後才送出",out);
    }
    {   out<<"\n== 4 宿主回呼內重入 ==\n";World w;AR in_action{},in_dispatch{};int inner_deliver=-1;
        setup(w,[&](World&,const Notification&,const AckToken&){in_action=w.set_value("A",1);in_dispatch=w.dispatch("B",[]{},{});inner_deliver=w.deliver();return HostReply::KeepProcess;});
        w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);
        w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();I(w,out);dump(w,out);
        check(in_action==AR::RefusedInHostCallback&&in_dispatch==AR::RefusedInHostCallback&&inner_deliver==0,"回呼內的 A 操作、派送重入、遞送重入都被拒絕，不遞迴、不死結",out);
        check(w.c.delivered==1&&w.set_value("B",2)==AR::Ok,"回呼結束後 B 可正常操作",out);
    }
    for(DiagMode m:{DiagMode::Disabled,DiagMode::Full,DiagMode::Throwing}){
        World w;int calls=0;setup(w,ack_in(calls));w.diag_mode=m;
        if(m==DiagMode::Full)for(int i=0;i<4;++i)w.diag.push_back("x");
        w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();I(w,out);
        out<<"\n== 5 診斷接收器 "<<(m==DiagMode::Disabled?"停用":m==DiagMode::Full?"已滿":"拋例外")<<" ==\n";dump(w,out);
        check(w.cur("A")->state==InstanceState::Unavailable&&calls==1&&w.c.released==1,"必要故障狀態、宿主交接與確認釋放不受診斷影響",out);
        if(m==DiagMode::Throwing)check(w.diag_disabled_after_throw&&w.c.diag_failures==1,"拋例外的接收器被停用，只算一次失敗",out);
    }
    {   out<<"\n== 6 宿主選擇：保留程序 vs 模擬退出 ==\n";
        for(HostReply r:{HostReply::KeepProcess,HostReply::RequestExit}){World w;setup(w,[r](World&,const Notification&,const AckToken&){return r;});
            w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();I(w,out);dump(w,out);
            check(w.process_exit_requested==(r==HostReply::RequestExit)&&w.cur("A")->state==InstanceState::Unavailable&&first(w)->phase==Handoff::AwaitingAck,
                  r==HostReply::KeepProcess?"保留程序：A 仍不可用；回覆不是確認":"模擬退出：只記錄，不實際退出；回覆不是確認",out);}
    }
    {   out<<"\n== 7 晚到結果：舊 A 不可用時、A 重建後 ==\n";World w;int calls=0;setup(w,ack_in(calls));w.add_env("A",{"svcA"},false);
        Token t1,t2;w.start_op("A",t1);w.start_op("A",t2);const auto old_id=w.cur("A")->id;
        w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();
        w.ledger->publish(t1,42);w.settle();
        check(w.find(old_id)->value==0&&w.c.late_rejected_unavailable==1&&w.find(old_id)->reservations==1,"舊 A 不可用：晚到結果不更新 UI，只結清 1 個預留",out);
        check(!w.rebuild("A",{}).has_value(),"共享服務仍不可信：拒絕重建",out);
        w.replace_service("svcA");auto nid=w.rebuild("A",{});I(w,out);
        check(nid&&*nid!=old_id&&w.cur("A")->gen==2&&w.cur("A")->state==InstanceState::Running,"明確替換服務後重建：新實例、新世代",out);
        check(w.find(old_id)&&w.find(old_id)->state==InstanceState::Unavailable,"舊實例仍不可用且仍在等待收尾",out);
        w.ledger->publish(t2,99);w.settle();I(w,out);dump(w,out);
        check(w.cur("A")->value==0&&!w.find(old_id)&&w.c.retired==1,"重建後才到的舊結果不更新新實例；舊實例結清後退休",out);
        check(w.ledger->used()==0&&w.ledger->released==2,"兩個槽位各歸還一次",out);
    }
    {   out<<"\n== 8 重複／過期／外來結果；目標世代（實例仍執行中）==\n";World w;int c1=0,c2=0;setup(w,keep(c1));w.add_env("A",{"svcA"},false);
        World other;setup(other,keep(c2));other.add_env("A",{"s"},false);
        Token t;w.start_op("A",t);
        check(w.ledger->publish(t,1)&&!w.ledger->publish(t,1),"重複發布被拒絕",out);
        Token foreign;other.start_op("A",foreign);
        check(!w.ledger->publish(foreign,1)&&!w.ledger->publish({w.ledger->id,t.instance,99,1},1),"外來帳本、越界槽位都被拒絕",out);
        w.settle();check(w.c.late_applied==1&&w.cur("A")->value==1,"有效結果套用",out);
        check(!w.ledger->publish(t,5),"已結算的 token 過期",out);
        Token g;w.start_op("A",g);w.rebuild_target("A");w.ledger->publish(g,7);w.settle();dump(w,out);
        check(w.cur("A")->state==InstanceState::Running&&w.cur("A")->value==0&&w.c.late_rejected_generation==1&&w.c.late_rejected_unavailable==0,"只因目標世代不符而拒絕（與不可用分開計數）",out);
    }
    {   out<<"\n== 9 重建被拒：條件不成立、實例容量已滿 ==\n";World w;int c=0;setup(w,keep(c));w.add_env("A",{"svcA"},false);Token t;w.start_op("A",t);
        w.dispatch("A",thrower,unknown_shared("svcA"));w.replace_service("svcA");
        check(!w.rebuild("A",{false,true})&&!w.rebuild("A",{true,false}),"隔離或資源條件不成立：拒絕",out);
        World full;int fc=0;setup(full,keep(fc));full.add_env("A",{"s"},false);full.add_env("B",{"b"},true);full.add_env("C",{"c"},true);full.add_env("D",{"d"},true);
        Token ft;full.start_op("A",ft);full.dispatch("A",thrower,unknown_shared("s"));full.replace_service("s");
        const auto s=snap(full);
        check(!full.rebuild("A",{})&&full.c.rebuilds_refused==1&&snap(full)==s,"實例容量已滿：拒絕重建，狀態不變",out);I(full,out);dump(full,out);
    }
    {   out<<"\n== 10 有界重複故障 ==\n";World w;int calls=0;setup(w,keep(calls));w.add_env("A",{"svcA"},false);
        w.dispatch("A",thrower,unknown_shared("svcA"));for(int i=0;i<1000;++i)w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();
        check(calls==1&&open_count(w)==1&&w.log.size()<=World::log_cap&&w.diag.size()<=World::diag_cap,"1000 次重複嘗試：1 次宿主回呼；日誌與診斷都有上限",out);
        w.log.clear();
    }
    {   out<<"\n== 11 確認前宿主回呼拋例外（v4：保留交接與額度）==\n";World w;int calls=0;AckToken tok{};
        setup(w,[&](World&,const Notification&,const AckToken& t)->HostReply{++calls;tok=t;throw std::runtime_error("host failed");});
        w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();w.deliver();I(w,out);dump(w,out);
        check(calls==1&&w.c.handoff_failed==1&&w.c.delivered==0,"只在宿主邊界攔截；回報失敗、不重跑",out);
        check(first(w)&&first(w)->phase==Handoff::AwaitingTakeover&&w.held_credits()==1&&w.c.released==0,"交接未完成、額度保留，等待擁有者改派",out);
        check(w.confirm(tok)==AR::RefusedRevoked&&w.c.released==0,"失敗的接收途徑之後不能再確認",out);
        check(w.cur("A")->state==InstanceState::Unavailable,"A 仍不可用",out);
    }
    {   out<<"\n== 12 未宣告安全交接路徑 ==\n";World w;int calls=0;setup(w,keep(calls));w.add_env("A",{"svcA"},false);
        FaultSpec s=unknown_shared("svcA");s.handoff_path_safe=false;w.dispatch("A",thrower,s);w.deliver();dump(w,out);
        check(w.out_of_scope&&calls==0&&w.set_value("A",1)==AR::RefusedOutOfScope&&!w.rebuild("A",{}),"標記超出範圍：不通知、不宣稱恢復、不重建（致命路徑不在本原型）",out);
    }
    {   out<<"\n== 13 回呼例外但宣告狀態仍有效 ==\n";World w;int c=0;setup(w,keep(c));w.add_env("A",{"svcA"},false);FaultSpec s;s.state_still_valid=true;
        check(w.dispatch("A",thrower,s)==AR::Failed&&w.cur("A")->state==InstanceState::Running&&open_count(w)==0,"只回報失敗結果，不停止範圍",out);
    }
    {   out<<"\n== 14 真實 std::thread 晚到結果 ==\n";World w;int c=0;setup(w,keep(c));w.add_env("A",{"svcA"},false);Token t;w.start_op("A",t);const auto old_id=w.cur("A")->id;
        auto ledger=w.ledger;std::atomic<bool> go{false},done{false};
        std::thread worker([ledger,t,&go,&done]{while(!go.load())std::this_thread::yield();ledger->publish(t,123);done.store(true);});  // token + ledger only
        w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();go.store(true);
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!done.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
        const bool finished=done.load();worker.join();
        int from_worker=-1;std::thread probe([&]{from_worker=w.settle();});probe.join();  // non-owner settle must be refused
        const size_t used_after_probe=w.ledger->used();const int unavail_after_probe=w.c.late_rejected_unavailable;
        w.settle();dump(w,out);
        check(finished,"worker 在期限內完成（期限只防止卡住，不是正確性條件）",out);
        check(from_worker==0&&used_after_probe==1&&unavail_after_probe==0,"非擁有者執行緒的結算被拒絕，槽位與計數都沒被動到",out);
        check(w.find(old_id)->value==0&&w.c.late_rejected_unavailable==1&&w.ledger->used()==0,"擁有者執行緒結算：不更新不可用的舊 A，槽位歸還",out);
    }
    {   out<<"\n== 15 宿主回呼內的變更入口、非擁有者執行緒 ==\n";World w;
        size_t inst_in_cb=0,used_in_cb=0;int settled_in_cb=-1;bool rebuilt_in_cb=false,added_in_cb=false;AR act_in_cb{},reasg_in_cb{};
        setup(w,[&](World&,const Notification& n,const AckToken&){
            w.replace_service("svcA");rebuilt_in_cb=w.rebuild("A",{}).has_value();settled_in_cb=w.settle();
            added_in_cb=w.add_env("Z",{"z"},true)==AR::Ok;w.service_fault("svcB");act_in_cb=w.activate(n.handoff,1);
            reasg_in_cb=w.reassign(n.handoff,w.active_receiver)?AR::Ok:AR::RefusedInHostCallback;
            inst_in_cb=w.instances.size();used_in_cb=w.ledger->used();return HostReply::KeepProcess;});
        w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);
        Token t;w.start_op("A",t);w.ledger->publish(t,5);
        w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();I(w,out);dump(w,out);
        check(!rebuilt_in_cb&&!added_in_cb&&settled_in_cb==0&&inst_in_cb==2&&used_in_cb==1,"回呼內重建／新增環境／結算都被拒絕：實例數、槽位不變",out);
        check(act_in_cb==AR::RefusedInHostCallback&&reasg_in_cb!=AR::Ok&&first(w)->req.s==Reassign::None,"回呼內改派與生效（結構變更）被拒絕",out);
        check(!w.services["svcA"].trustworthy&&w.cur("B")->state==InstanceState::Running&&w.cur("A")->state==InstanceState::Unavailable,"回呼內替換服務、宣告服務故障都沒有生效",out);
        World o;int oc=0;setup(o,keep(oc));o.add_env("A",{"svcA"},false);int before=o.cur("A")->value;AR r{};
        std::thread off([&]{r=o.set_value("A",7);});off.join();
        check(r==AR::RefusedWrongThread&&o.cur("A")->value==before&&o.c.refused_actions==0,"非擁有者執行緒的設值被拒絕，UI 狀態與計數都不變",out);
        World s;int sc=0;setup(s,keep(sc));s.add_env("A",{"svcA"},false);s.traversals=1;const auto svc_gen=s.services["svcA"].gen;
        check(s.replace_service("svcA")==AR::RefusedNotSafePoint&&s.services["svcA"].gen==svc_gen&&s.add_env("Q",{"q"},true)==AR::RefusedNotSafePoint&&s.instances.size()==1,
              "遍歷進行中：替換服務、新增環境被拒絕（fixture 規則：非安全點直接拒絕）",out);
    }
    out<<"\n== 16 （v3 的「不遞送循環」已由 v4 情境 26 取代：額度現在要確認後才回收）==\n";
    {   out<<"\n== 17 錯誤預留世代打到仍在預留中的槽位 ==\n";World w;int c=0;setup(w,keep(c));w.add_env("A",{"svcA"},false);Token t;w.start_op("A",t);
        Token bad=t;bad.gen=t.gen+1;
        check(!w.ledger->publish(bad,1)&&w.ledger->used()==1&&w.ledger->published==0,"錯誤世代被拒絕，槽位仍是預留中、未發布",out);
        check(w.ledger->publish(t,2),"之後有效發布成功",out);w.settle();check(w.cur("A")->value==2,"有效結果套用",out);
    }
    {   out<<"\n== 18 未宣告安全交接後，已預留結果不得更新 UI ==\n";World w;int c=0;setup(w,keep(c));w.add_env("A",{"svcA"},false);Token t;w.start_op("A",t);
        FaultSpec s=unknown_shared("svcA");s.handoff_path_safe=false;w.dispatch("A",thrower,s);
        w.ledger->publish(t,9);w.settle();dump(w,out);
        check(w.cur("A")->value==0&&w.ledger->used()==1&&w.c.held_out_of_scope==1,"超出範圍：結果不套用、預留保留（不宣稱已安全收尾）",out);
        check(w.set_value("A",3)==AR::RefusedOutOfScope&&w.cur("A")->value==0,"超出範圍後普通操作也被拒絕；此狀態刻意未解決",out);
    }
    {   out<<"\n== 19 已有未解決交接後才進入未宣告安全交接 ==\n";World w;int calls=0;std::vector<AckToken> toks;setup(w,noack(calls,toks));
        w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);Token ta;w.start_op("A",ta);
        w.dispatch("A",thrower,unknown_shared("svcA"));
        FaultSpec unsafe=unknown_shared("svcB");unsafe.handoff_path_safe=false;w.dispatch("B",thrower,unsafe);
        const auto s=snap(w);
        w.ledger->publish(ta,5);const int n=w.deliver();w.settle();w.cleanup();dump(w,out);
        check(calls==0&&n==0&&w.c.delivered==0,"超出範圍：不再呼叫宿主（既有交接不送出）",out);
        check(snap(w)==s&&w.ledger->published_waiting()==1,"交接、額度、實例保持原狀；已發布結果不結算",out);
    }
    {   out<<"\n== 20 沒有有效接收者（v4：啟用前就拒絕）==\n";World w;const auto s=snap(w);
        check(w.add_env("A",{"svcA"},false)==AR::RefusedNoReceiver&&snap(w)==s&&w.instances.empty()&&!w.services.count("svcA"),"沒有接收者：明確拒絕啟用，狀態不變",out);
        int c=0;w.add_receiver("host",keep(c));const auto s2=snap(w);
        check(w.add_env("A",{"svcA"},false)==AR::RefusedNoReceiver&&snap(w)==s2,"只登記、未設為主動接收途徑：仍拒絕",out);dump(w,out);
    }
    {   out<<"\n== 21 重複的環境名稱 ==\n";World w;int c=0;setup(w,keep(c));w.add_env("A",{"svcA"},false);Token t;w.start_op("A",t);
        const auto s=snap(w);
        check(w.add_env("A",{"other"},true)==AR::RefusedDuplicate,"重複名稱被明確拒絕",out);
        check(snap(w)==s&&!w.services.count("other"),"原實例、額度、current 對應、服務表都不變",out);
    }
    // ---- 22..28: v4 explicit acknowledgment and reassignment ----
    {   out<<"\n== 22 回呼返回但未確認：額度保留；稍後確認釋放一次 ==\n";World w;int calls=0;std::vector<AckToken> toks;setup(w,noack(calls,toks));
        w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));I(w,out);w.deliver();I(w,out);
        check(calls==1&&first(w)->phase==Handoff::AwaitingAck&&w.held_credits()==1&&w.c.released==0&&w.notice_used==1,"一般返回不是確認：待確認、額度保留",out);
        w.add_env("B",{"b"},true);w.add_env("C",{"c"},true);w.add_env("D",{"d"},true);I(w,out);
        const auto s=snap(w);
        check(w.add_env("E",{"e"},true)==AR::RefusedNoCapacity&&snap(w)==s,"容量已滿：新增環境在任何部分變更前就被拒絕",out);
        w.replace_service("svcA");const auto s1=snap(w);check(!w.rebuild("A",{})&&snap(w)==s1,"容量已滿：重建被拒，狀態不變",out);
        check(w.confirm(toks[0])==AR::Ok&&w.c.released==1&&w.held_credits()==0&&w.notice_used==3,"延後的明確確認：釋放一次",out);I(w,out);
        const auto s2=snap(w);const int rel=w.c.released;
        check(w.confirm(toks[0])==AR::RefusedStale&&w.confirm(toks[0])==AR::RefusedStale,"重複確認被拒",out);
        w.cleanup();w.cleanup();w.deliver();w.settle();
        check(snap(w)==s2&&w.c.released==rel&&calls==1,"重複確認與重複收尾都不改額度與釋放次數",out);I(w,out);
        check(w.cur("A")->state==InstanceState::Unavailable,"確認不讓 UI 復活",out);dump(w,out);
    }
    {   out<<"\n== 23 回呼內確認；確認後拋例外（安全／不安全）；確認前拋例外 ==\n";
        {   World w;int calls=0;Handoff::Phase in_phase{};size_t held_in=0;AR r{};
            setup(w,[&](World& x,const Notification&,const AckToken& t){++calls;r=x.confirm(t);in_phase=first(x)->phase;held_in=x.held_credits();return HostReply::KeepProcess;});
            w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();I(w,out);dump(w,out);
            check(r==AR::Ok&&in_phase==Handoff::Confirmed&&held_in==1,"回呼內確認：已確認，但回呼未結束前額度仍保留",out);
            check(w.c.released==1&&w.held_credits()==0&&open_count(w)==0,"回呼安全返回後釋放一次",out);}
        {   World w;int calls=0;
            setup(w,[&](World& x,const Notification&,const AckToken& t)->HostReply{++calls;x.confirm(t);throw std::runtime_error("after ack");},true);
            w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();w.deliver();I(w,out);dump(w,out);
            check(w.c.confirmed==1&&w.c.failed_after_ack==1&&w.c.released==1&&calls==1&&w.c.handoff_failed==0,"確認後拋例外（宣告安全）：確認保留、另報失敗、釋放一次、不重送",out);}
        {   World w;int calls=0;AckToken tok{};
            setup(w,[&](World& x,const Notification&,const AckToken& t)->HostReply{++calls;tok=t;if(calls>99)x.confirm(t);throw std::runtime_error("before ack");});
            w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();w.deliver();I(w,out);dump(w,out);
            check(first(w)->phase==Handoff::AwaitingTakeover&&w.held_credits()==1&&w.c.released==0&&calls==1,"確認前拋例外：未解決、額度保留、不重跑",out);}
        {   World w;int calls=0;AckToken tok{};
            setup(w,[&](World& x,const Notification&,const AckToken& t)->HostReply{++calls;tok=t;x.confirm(t);throw std::runtime_error("unsafe after ack");},false);
            w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();I(w,out);
            const auto s=snap(w);w.deliver();w.cleanup();const AR again=w.confirm(tok);I(w,out);dump(w,out);
            check(first(w)->phase==Handoff::Confirmed&&!first(w)->release_safe&&w.held_credits()==1&&w.c.released==0,"確認後不安全失敗（宣告）：確認保留，額度無限期保留且可見",out);
            check(w.out_of_scope&&again==AR::RefusedOutOfScope&&snap(w)==s,"v5：同時標記超出範圍；之後的重送、收尾、重複確認都不會釋放",out);}
    }
    {   out<<"\n== 24 舊工作仍在時改派：撤銷、等待、生效 ==\n";auto p=rig(false);Rig& g=*p;World& w=g.w;I(w,out);
        check(g.r1!=g.r2&&w.receivers[g.r1].name==w.receivers[g.r2].name,"同名接收者替換：不同身分（防 ABA）",out);
        check(g.wk.has_value()&&first(w)->phase==Handoff::AwaitingAck,"舊接收者回呼開始一項列管工作，未確認",out);
        auto r=w.reassign(g.h,g.r2);g.req=r?*r:0;I(w,out);
        const auto s0=snap(w);
        check(r&&w.confirm(g.t1)==AR::RefusedRevoked&&snap(w)==s0,"受理改派立即撤銷舊確認資格；被拒的確認不改狀態",out);
        check(w.confirm({w.id,g.h,g.req})!=AR::Ok&&g.calls2==0,"生效前沒有人能確認（拿要求編號冒充也被拒）",out);
        check(w.activate(g.h,g.req)==AR::RefusedNotEligible&&first(w)->req.s==Reassign::Waiting,"舊工作未結束：不能生效（不阻塞，直接回報）",out);
        w.end_work(*g.wk);I(w,out);
        check(w.confirm(g.t1)==AR::RefusedRevoked&&first(w)->req.s==Reassign::Waiting&&w.c.activated==0,"舊工作結束：不恢復舊資格，也不自動生效",out);
        check(w.activate(g.h,g.req)==AR::Ok,"擁有者在安全點明確生效",out);I(w,out);
        w.deliver();I(w,out);
        check(g.calls2==1&&g.t2.assignment!=g.t1.assignment&&first(w)->phase==Handoff::AwaitingAck&&w.held_credits()==1,"交接送給新接管者，新指派身分不同；額度仍保留",out);
        World other;int oc=0;setup(other,keep(oc));AckToken foreign=g.t2;foreign.world=other.id;
        const auto s1=snap(w);
        check(w.confirm(g.t1)==AR::RefusedRevoked&&w.confirm(foreign)==AR::RefusedInvalid&&snap(w)==s1,"舊憑證、外來 World 憑證沒有效果",out);
        check(w.confirm(g.t2)==AR::Ok&&w.c.released==1&&w.held_credits()==0&&w.notice_used==0,"新憑證確認一次 → 釋放一次",out);I(w,out);
        const auto s2=snap(w);
        check(w.confirm(g.t2)==AR::RefusedStale&&w.confirm(g.t1)==AR::RefusedStale&&snap(w)==s2&&w.c.released==1,"重複與晚到的確認沒有效果",out);
        check(w.cur("A")->state==InstanceState::Unavailable,"確認不代表修復：A 仍不可用",out);dump(w,out);
    }
    {   out<<"\n== 25 取消與生效的順序、第二個要求、候選失效 ==\n";
        {   auto p=rig();Rig& g=*p;World& w=g.w;w.end_work(*g.wk);
            const AR c=w.cancel_reassign(g.h,g.req),a=w.activate(g.h,g.req);I(w,out);dump(w,out);
            check(c==AR::Ok&&a==AR::RefusedStale&&w.c.cancelled==1&&w.c.activated==0,"已具備生效條件但先處理取消：取消勝出、生效被拒（唯一結局）",out);
            check(first(w)->phase==Handoff::AwaitingTakeover&&w.held_credits()==1&&w.confirm(g.t1)==AR::RefusedRevoked&&g.calls2==0,"取消後：待接管、額度保留、舊資格不恢復",out);}
        {   auto p=rig();Rig& g=*p;World& w=g.w;w.end_work(*g.wk);
            const AR a=w.activate(g.h,g.req),c=w.cancel_reassign(g.h,g.req);w.deliver();I(w,out);dump(w,out);
            check(a==AR::Ok&&c==AR::TooLate&&w.c.activated==1&&w.c.cancelled==0&&w.c.too_late==1,"先處理生效：取消回報太晚（唯一結局）",out);
            check(g.calls2==1&&w.confirm(g.t2)==AR::Ok&&w.c.released==1,"新接管者確認 → 釋放一次",out);I(w,out);}
        {   auto p=rig();Rig& g=*p;World& w=g.w;
            check(w.cancel_reassign(g.h,g.req)==AR::Ok&&first(w)->find(g.t1.assignment)->work==1,"舊工作仍在時取消：舊工作繼續被列管",out);
            int c3=0;const auto r3=w.add_receiver("host",keep(c3));const auto q=w.reassign(g.h,r3);
            check(q&&*q!=g.req&&w.activate(g.h,*q)==AR::RefusedNotEligible,"前一要求結束後可送新候選，但仍要等舊工作",out);
            w.end_work(*g.wk);check(w.activate(g.h,*q)==AR::Ok&&w.confirm(g.t1)==AR::RefusedRevoked,"舊工作結束後新要求生效；舊憑證仍無效",out);I(w,out);dump(w,out);}
        {   auto p=rig();Rig& g=*p;World& w=g.w;const auto s=snap(w);
            check(!w.reassign(g.h,g.r1)&&first(w)->req.id==g.req&&w.c.reassign_refused==1&&snap(w)==s,"已有未完成改派：第二個要求不受理、不覆寫",out);dump(w,out);}
        {   auto p=rig(false);Rig& g=*p;World& w=g.w;
            check(w.confirm(g.t1)==AR::Ok&&!w.reassign(g.h,g.r2)&&w.c.released==1,"順序甲：先確認（資格有效）→ 交接完成，之後的改派不受理",out);dump(w,out);}
        {   auto p=rig();Rig& g=*p;World& w=g.w;
            check(w.confirm(g.t1)==AR::RefusedRevoked&&w.c.confirmed==0,"順序乙：先受理改派 → 舊確認被拒",out);dump(w,out);}
        {   auto p=rig();Rig& g=*p;World& w=g.w;w.end_work(*g.wk);w.bump_receiver_version(g.r2);
            check(w.activate(g.h,g.req)==AR::Failed&&first(w)->req.s==Reassign::Failed&&first(w)->phase==Handoff::AwaitingTakeover&&w.held_credits()==1,"生效時重新驗證：候選版本改變 → 改派失敗，待接管、額度保留",out);
            const auto q=w.reassign(g.h,g.r2);check(q&&w.activate(g.h,*q)==AR::Ok,"擁有者重新送出要求（新版本）→ 生效",out);I(w,out);dump(w,out);}
        {   auto p=rig();Rig& g=*p;World& w=g.w;w.receiver_lost(g.r2);I(w,out);
            check(first(w)->req.s==Reassign::Failed&&w.c.reassign_failed==1&&w.held_credits()==1&&w.confirm(g.t1)==AR::RefusedRevoked,"等待中候選失效：明確失敗、額度保留、舊資格不恢復",out);dump(w,out);}
    }
    {   out<<"\n== 26 反覆循環超過容量：儲存有界、額度最終回收、舊憑證不被接受 ==\n";
        {   World w;int calls=0;std::vector<AckToken> toks;
            setup(w,[&](World& x,const Notification&,const AckToken& t){++calls;toks.push_back(t);x.confirm(t);return HostReply::KeepProcess;});
            w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);size_t max_open=0;
            for(int i=0;i<10;++i){w.dispatch("A",thrower,unknown_shared("svcA"));I(w,out);max_open=std::max(max_open,open_count(w));
                w.deliver();I(w,out);w.replace_service("svcA");w.rebuild("A",{});I(w,out);w.settle();I(w,out);}
            const auto s=snap(w);int stale=0;for(auto& t:toks)stale+=w.confirm(t)==AR::RefusedStale;
            check(calls==10&&w.c.released==10&&w.notice_used==2&&max_open<=1,"10 次故障／確認／重建：各釋放一次，額度回到執行中的 2",out);
            check(stale==10&&snap(w)==s,"槽位重用後，舊憑證全部被拒、沒有效果",out);
            check(w.instances.size()<=World::instance_cap&&w.log.size()<=World::log_cap,"實例與日誌有界（沒有無界完成歷史）",out);w.log.clear();}
        {   World w;int calls=0;std::vector<AckToken> toks;setup(w,noack(calls,toks));w.add_env("A",{"svcA"},false);int refused=0;size_t max_open=0;
            for(int i=0;i<8;++i){w.dispatch("A",thrower,unknown_shared("svcA"));I(w,out);max_open=std::max(max_open,open_count(w));w.deliver();I(w,out);
                w.replace_service("svcA");if(!w.rebuild("A",{}))++refused;I(w,out);}
            out<<"未確認交接="<<open_count(w)<<" 拒絕重建="<<refused<<" 額度="<<w.notice_used<<"/"<<World::notice_cap<<"\n";
            check(max_open<=World::notice_cap&&refused>0&&w.notice_used==World::notice_cap,"不確認：交接額度用盡，重建被明確拒絕",out);
            const auto first_tok=toks.front();check(w.confirm(first_tok)==AR::Ok,"確認第一個交接",out);I(w,out);
            w.replace_service("svcA");check(w.rebuild("A",{}).has_value(),"釋放一個額度後重建成功",out);I(w,out);
            w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();I(w,out);
            check(w.confirm(first_tok)==AR::RefusedStale&&w.c.released==1,"交接槽位被新交接重用：舊憑證仍不被接受",out);
            for(auto& t:toks)w.confirm(t);I(w,out);
            check(w.held_credits()==0&&w.notice_used==w.running()&&w.c.released==(int)toks.size(),"全部確認後額度完全回收，每個交接釋放一次",out);w.log.clear();}
    }
    {   out<<"\n== 27 真實 worker 透過信箱送控制訊息（擁有者執行緒 FIFO 處理）==\n";
        auto post_from_worker=[](std::shared_ptr<ControlBox> box,std::vector<Control> msgs){bool ok=true;std::thread t([&]{for(auto& m:msgs)ok&=box->post(m);});t.join();return ok;};
        {   auto p=rig();Rig& g=*p;World& w=g.w;const WorkToken wk=*g.wk;
            const bool posted=post_from_worker(w.control,{{Control::EndWork,{},0,0,wk},{Control::Activate,{},g.h,g.req,{}}});
            check(posted&&first(w)->req.s==Reassign::Waiting&&first(w)->find(wk.assignment)->work==1,"worker 只投遞訊息，不直接改 World",out);
            const int n=w.process_control();I(w,out);dump(w,out);
            check(n==2&&w.c.activated==1,"FIFO：先結束舊工作、再生效 → 生效",out);}
        {   auto p=rig();Rig& g=*p;World& w=g.w;const WorkToken wk=*g.wk;
            post_from_worker(w.control,{{Control::Activate,{},g.h,g.req,{}},{Control::EndWork,{},0,0,wk}});
            const int n=w.process_control();I(w,out);dump(w,out);
            check(n==2&&w.c.activated==0&&first(w)->req.s==Reassign::Waiting&&first(w)->old_work_ended(),"FIFO：生效先處理被拒；之後舊工作結束只是「可生效」，不等於已生效",out);
            check(w.activate(g.h,g.req)==AR::Ok,"擁有者再明確生效",out);}
        {   auto p=rig();Rig& g=*p;World& w=g.w;w.end_work(*g.wk);
            post_from_worker(w.control,{{Control::Cancel,{},g.h,g.req,{}},{Control::Activate,{},g.h,g.req,{}}});w.process_control();I(w,out);dump(w,out);
            check(w.c.cancelled==1&&w.c.activated==0,"信箱中取消在前：取消勝出",out);}
        {   auto p=rig();Rig& g=*p;World& w=g.w;w.end_work(*g.wk);
            post_from_worker(w.control,{{Control::Activate,{},g.h,g.req,{}},{Control::Cancel,{},g.h,g.req,{}}});w.process_control();I(w,out);dump(w,out);
            check(w.c.activated==1&&w.c.cancelled==0&&w.c.too_late==1,"信箱中生效在前：生效勝出，取消太晚",out);}
        {   auto p=rig(false);Rig& g=*p;World& w=g.w;AR r{};const auto s=snap(w);const int rej=w.c.confirm_rejected;
            std::thread off([&]{r=w.confirm(g.t1);});off.join();
            check(r==AR::RefusedWrongThread&&snap(w)==s&&w.c.confirm_rejected==rej,"worker 直接呼叫確認：拒絕，狀態與計數不變",out);
            post_from_worker(w.control,{{Control::Confirm,g.t1,0,0,{}}});w.process_control();I(w,out);dump(w,out);
            check(w.c.confirmed==1&&w.c.released==1,"經信箱送確認 → 擁有者執行緒處理 → 釋放一次",out);}
    }
    {   out<<"\n== 28 接收者計畫性撤除與意外失效 ==\n";World w;int calls=0;std::vector<AckToken> toks;const auto r1=setup(w,noack(calls,toks));w.add_env("A",{"svcA"},false);
        check(w.detach_receiver(r1)==AR::RefusedDependents&&w.active_receiver==r1,"仍有執行中相依環境：拒絕撤除（fixture 窄規則）",out);
        const auto r2=w.add_receiver("host",noack(calls,toks));w.set_active_receiver(r2);
        check(w.detach_receiver(r1)==AR::Ok&&w.active_receiver==r2,"先設定替代接收者，再撤除舊的",out);
        w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();I(w,out);
        check(w.detach_receiver(r2)==AR::RefusedDependents,"仍承擔未確認交接：拒絕撤除",out);
        w.receiver_lost(r2);I(w,out);
        check(w.c.receiver_lost==1&&first(w)->phase==Handoff::AwaitingTakeover&&w.held_credits()==1&&w.active_receiver==0,"確認前接收者意外失效：算失敗，保留交接與額度",out);
        check(w.confirm(toks.back())==AR::RefusedRevoked,"失效接收者的憑證被拒",out);
        const auto s=snap(w);
        check(w.add_env("B",{"b"},true)==AR::RefusedNoReceiver&&snap(w)==s,"沒有接收途徑：新增環境被拒、狀態不變",out);
        check(w.deliver()==0&&calls==1,"不自動重送、不重試",out);
        int c3=0;std::vector<AckToken> t3;const auto r3=w.add_receiver("host",noack(c3,t3));w.set_active_receiver(r3);
        const auto q=w.reassign(first(w)->id,r3);w.activate(first(w)->id,q?*q:0);w.deliver();I(w,out);
        check(c3==1&&w.confirm(t3.back())==AR::Ok&&w.c.released==1,"擁有者明確改派後，新接管者確認 → 釋放一次",out);I(w,out);dump(w,out);
    }
    // ---- 29..33: review regressions on v4 ----
    {   out<<"\n== 29 列管工作身分：兩項舊工作、重複結束、取消後重新要求、槽位重用 ==\n";
        World w;std::vector<WorkToken> jobs;std::vector<AckToken> toks;
        setup(w,[&](World& x,const Notification&,const AckToken& t){toks.push_back(t);for(int i=0;i<2;++i)if(auto j=x.begin_work(t))jobs.push_back(*j);return HostReply::KeepProcess;});
        int c2=0;const auto r2=w.add_receiver("host",keep(c2));
        w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();I(w,out);
        const auto h=first(w)->id;const auto q=w.reassign(h,r2);
        check(jobs.size()==2&&q,"舊回呼開始兩項列管工作；改派受理",out);
        const AR e1=w.end_work(jobs[0]),e2=w.end_work(jobs[0]);I(w,out);
        check(e1==AR::Ok&&e2!=AR::Ok,"第一項結束一次成功；重複結束被拒（不能扣到第二項）",out);
        check(w.activate(h,q?*q:0)==AR::RefusedNotEligible,"第二項仍在：生效被拒（舊工作含被撤銷的目前指派）",out);
        w.cancel_reassign(h,q?*q:0);const auto q2=w.reassign(h,r2);
        check(q2&&w.activate(h,*q2)==AR::RefusedNotEligible,"取消並重新要求：仍等第二項",out);
        check(w.end_work(jobs[1])==AR::Ok&&w.activate(h,q2?*q2:0)==AR::Ok,"第二項結束後才生效",out);
        check(w.end_work(jobs[1])!=AR::Ok&&w.end_work(jobs[0])!=AR::Ok,"已結束的工作身分再結束都被拒",out);I(w,out);dump(w,out);
        World u;std::vector<WorkToken> uj;std::vector<AckToken> ut;
        setup(u,[&](World& x,const Notification&,const AckToken& t){ut.push_back(t);if(auto j=x.begin_work(t))uj.push_back(*j);return HostReply::KeepProcess;});
        u.add_env("A",{"svcA"},false);u.dispatch("A",thrower,unknown_shared("svcA"));u.deliver();u.confirm(ut[0]);
        u.replace_service("svcA");u.rebuild("A",{});u.dispatch("A",thrower,unknown_shared("svcA"));u.deliver();I(u,out);
        const auto s=snap(u);
        check(uj.size()==2&&u.end_work(uj[0])!=AR::Ok&&snap(u)==s,"交接槽位重用後：舊工作身分不影響新交接的列管工作",out);
        check(u.end_work(uj[1])==AR::Ok,"新交接的工作正常結束",out);u.log.clear();
    }
    {   out<<"\n== 30 宣告不安全的回呼失敗（確認前／確認後）==\n";
        {   World w;int calls=0;std::vector<AckToken> toks;
            setup(w,[&](World&,const Notification&,const AckToken& t)->HostReply{++calls;toks.push_back(t);throw std::runtime_error("unsafe before ack");},false);
            int c2=0;const auto r2=w.add_receiver("host",keep(c2));
            w.add_env("A",{"a"},true);w.add_env("B",{"b"},true);w.service_fault("a");w.service_fault("b");I(w,out);
            check(open_count(w)==2,"兩個未解決交接",out);
            w.deliver();I(w,out);
            check(calls==1&&w.out_of_scope,"確認前不安全失敗：標記超出範圍，同一輪不再呼叫第二個回呼",out);
            check(w.held_credits()==2&&w.c.released==0,"兩個交接的額度都保留",out);
            const auto s=snap(w);const auto hid0=first(w)->id;
            const auto q=w.reassign(hid0,r2);const auto bw=w.begin_work(toks[0]);
            w.control->post({Control::Cancel,{},hid0,1,{}});const int n=w.process_control();w.deliver();w.cleanup();
            check(!q&&!bw&&n==0&&w.control->q.size()==1,"之後改派、開始工作、信箱處理都被拒，信箱不被清空",out);
            check(snap(w)==s&&calls==1&&c2==0,"狀態不變、不再呼叫任何宿主回呼",out);dump(w,out);}
        {   World w;int calls=0;std::vector<AckToken> toks;std::optional<WorkToken> job;
            const auto r1=setup(w,[&](World& x,const Notification&,const AckToken& t)->HostReply{++calls;toks.push_back(t);
                if(calls==1){job=x.begin_work(t);return HostReply::KeepProcess;}throw std::runtime_error("unsafe");},false);
            int c2=0;const auto r2=w.add_receiver("host",keep(c2));(void)r1;
            w.add_env("A",{"a"},true);w.add_env("B",{"b"},true);w.service_fault("a");w.deliver();
            Handoff* ha=first(w);const auto hA=ha->id;const auto q=w.reassign(hA,r2);
            w.service_fault("b");w.deliver();I(w,out);
            check(w.out_of_scope&&q.has_value(),"另一個交接的回呼不安全失敗 → 超出範圍（A 的改派仍在等待）",out);
            const auto s=snap(w);
            const AR ew=w.end_work(*job),ac=w.activate(hA,*q),ca=w.cancel_reassign(hA,*q);
            check(ew!=AR::Ok&&ac!=AR::Ok&&ca!=AR::Ok&&snap(w)==s,"不安全標記後：結束舊工作、生效、取消都不動狀態（不排乾列管工作）",out);dump(w,out);}
        {   World w;int calls=0;
            setup(w,[&](World& x,const Notification&,const AckToken& t)->HostReply{++calls;x.confirm(t);throw std::runtime_error("unsafe after ack");},false);
            w.add_env("A",{"a"},true);w.add_env("B",{"b"},true);w.service_fault("a");w.service_fault("b");w.deliver();I(w,out);dump(w,out);
            check(calls==1&&w.out_of_scope&&w.c.confirmed==1,"確認後不安全失敗：確認保留、標記超出範圍、不呼叫第二個回呼",out);
            check(w.held_credits()==2&&w.c.released==0,"已確認交接的額度仍保留",out);}
    }
    {   out<<"\n== 31 非擁有者執行緒呼叫新控制入口：完全不動狀態與計數 ==\n";
        auto p=rig();Rig& g=*p;World& w=g.w;int dummy=0;
        auto full=[](World& x){std::ostringstream o;o<<snap(x)<<'#'<<x.log.size()<<'#'<<x.control->q.size()<<'#';
            const int* c=reinterpret_cast<const int*>(&x.c);for(size_t i=0;i<sizeof(x.c)/sizeof(int);++i)o<<c[i]<<',';
            for(auto& [id,r]:x.receivers)o<<id<<'v'<<r.version;return o.str();};
        const auto before=full(w);
        std::thread off([&]{w.reassign(g.h,g.r2);w.reassign(g.h,g.r1);w.cancel_reassign(g.h,g.req);w.activate(g.h,g.req);w.confirm(g.t1);w.begin_work(g.t1);
            w.end_work(*g.wk);w.process_control();w.add_receiver("x",keep(dummy));w.set_active_receiver(g.r2);w.detach_receiver(g.r2);w.receiver_lost(g.r2);
            w.bump_receiver_version(g.r2);w.deliver();w.settle();w.add_env("Z",{"z"},true);w.rebuild("A",{});w.service_fault("svcA");});
        off.join();
        check(full(w)==before,"所有新控制入口從非擁有者執行緒呼叫：狀態、計數、日誌、信箱、接收者版本都不變",out);
    }
    {   out<<"\n== 32 空的接收回呼不是有效接收者 ==\n";World w;
        const auto rid=w.add_receiver("empty",Callback{});const AR sr=w.set_active_receiver(rid);const auto s=snap(w);
        const AR a=w.add_env("A",{"svcA"},false);
        check(rid==0&&sr!=AR::Ok&&a==AR::RefusedNoReceiver&&w.receivers.empty()&&snap(w)==s&&w.instances.empty()&&w.notice_used==0,"空回呼被拒；不能啟用環境，額度與設定不變",out);
        int c=0;std::vector<AckToken> t;setup(w,noack(c,t));
        check(w.add_env("A",{"svcA"},false)==AR::Ok,"刻意不確認的接收者仍是有效接收者",out);I(w,out);dump(w,out);
    }
    {   out<<"\n== 33 交接表已滿時重複回報同一服務故障 ==\n";World w;int calls=0;std::vector<AckToken> toks;setup(w,noack(calls,toks));
        for(int i=1;i<=4;++i){const auto n=std::to_string(i);w.add_env("E"+n,{"s"+n},true);w.service_fault("s"+n);w.deliver();I(w,out);}
        check(open_count(w)==4&&w.notice_used==World::notice_cap,"四個未確認交接佔滿交接表",out);
        const auto s=snap(w);const int rep=w.c.repeated_fault_attempts;AR r=AR::Ok;bool threw=false;
        try{r=w.service_fault("s1");}catch(const std::exception& e){threw=true;out<<"例外："<<e.what()<<"\n";}
        check(!threw&&r!=AR::Ok&&w.c.repeated_fault_attempts==rep+1&&snap(w)==s,"重複回報：不配置紀錄、不丟例外、額度與身分不變，只計數",out);I(w,out);dump(w,out);
    }
    out<<"\n容量方程式檢查次數="<<inv_checks<<"\n";
    out<<"\nscenarios v5 "<<(failures?"FAIL":"PASS")<<" failures="<<failures<<'\n';
}

// ---------- interactive Win32 v4 (simulated; buttons call the model) ----------
static std::unique_ptr<World> live;static HWND status_box,list_box;static std::string guide;
enum class CbMode{NoAck,AckIn,AckThenThrow};static CbMode cb_mode=CbMode::NoAck;static bool track_work=false;
static std::vector<AckToken> seen;static std::optional<WorkToken> old_work;static std::uint64_t r1=0,r2=0,req_id=0;
static std::wstring widen(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n);return w;}
static Callback gui_cb(){return [](World& w,const Notification&,const AckToken& t)->HostReply{seen.push_back(t);
    if(track_work&&!old_work){old_work=w.begin_work(t);w.L("回呼開始一項列管工作");}
    if(cb_mode!=CbMode::NoAck)w.L(std::string("回呼內確認：")+name(w.confirm(t)));
    if(cb_mode==CbMode::AckThenThrow)throw std::runtime_error("host callback failed after ack");
    return HostReply::KeepProcess;};}
static std::uint64_t hid(World& w){Handoff* h=first(w);return h?h->id:0;}
static void fault_a(World& w){w.dispatch("A",thrower,unknown_shared("svcA"));}
static void deliver(World& w){w.L("遞送："+std::to_string(w.deliver())+" 個交接");}
static void confirm_new(World& w){if(seen.empty()){w.L("尚無確認憑證");return;}w.L(std::string("以最新憑證確認：")+name(w.confirm(seen.back())));}
static void confirm_old(World& w){if(seen.empty()){w.L("尚無確認憑證");return;}w.L(std::string("以最舊憑證確認：")+name(w.confirm(seen.front())));}
static void end_old(World& w){if(!old_work){w.L("沒有列管的舊工作");return;}w.L(std::string("舊工作結束：")+name(w.end_work(*old_work)));old_work.reset();track_work=false;}
static void do_reassign(World& w){auto r=w.reassign(hid(w),r2);if(r)req_id=*r;}
static void do_activate(World& w){w.L(std::string("改派生效：")+name(w.activate(hid(w),req_id)));}
static void do_cancel(World& w){w.L(std::string("取消改派：")+name(w.cancel_reassign(hid(w),req_id)));}
struct Step{const char* text;std::function<void(World&)> act;};
static std::vector<Step> steps;static size_t step_i=0;
static void reset(int kind){
    live=std::make_unique<World>();auto& w=*live;cb_mode=CbMode::NoAck;track_work=false;seen.clear();old_work.reset();req_id=0;step_i=0;
    r1=w.add_receiver("host",gui_cb());w.set_active_receiver(r1);r2=w.add_receiver("host",gui_cb());   // same name, distinct identity
    w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);
    const std::vector<Step> wait_steps={{"A 回呼故障",fault_a},{"遞送（舊接收者回呼開始列管工作，未確認）",deliver},{"受理改派（舊資格立即撤銷）",do_reassign},
        {"以舊憑證確認（被拒）",confirm_old},{"嘗試生效（舊工作未結束 → 拒絕）",do_activate},{"舊工作結束（只是可生效）",end_old}};
    switch(kind){
    case 2:cb_mode=CbMode::AckIn;guide="引導2：回呼內確認";
        steps={{"A 回呼故障",fault_a},{"遞送（回呼內確認；返回後才釋放）",deliver},{"再以同一憑證確認（無效）",confirm_new}};break;
    case 3:track_work=true;guide="引導3：改派 撤銷→等待→生效";steps=wait_steps;
        steps.insert(steps.end(),{{"舊憑證確認（仍被拒）",confirm_old},{"生效（明確提交）",do_activate},{"遞送給新接管者",deliver},
            {"舊憑證確認（被拒）",confirm_old},{"新憑證確認（釋放一次）",confirm_new}});break;
    case 4:track_work=true;guide="引導4：取消先處理";steps=wait_steps;
        steps.insert(steps.end(),{{"取消（先處理 → 勝出）",do_cancel},{"生效（被拒：過期）",do_activate},{"舊憑證確認（舊資格不恢復）",confirm_old}});break;
    case 5:track_work=true;guide="引導5：生效先處理";steps=wait_steps;
        steps.insert(steps.end(),{{"生效（先處理 → 勝出）",do_activate},{"取消（太晚）",do_cancel},{"遞送給新接管者",deliver},{"新憑證確認",confirm_new}});break;
    case 6:cb_mode=CbMode::AckThenThrow;guide="引導6：確認後回呼拋例外（宣告安全）";
        steps={{"A 回呼故障",fault_a},{"遞送（確認後拋例外：確認保留、釋放一次）",deliver},{"再遞送（不重送）",deliver}};break;
    default:guide="引導1：延後確認";
        steps={{"A 回呼故障",fault_a},{"遞送（回呼返回、未確認 → 額度保留）",deliver},{"稍後明確確認（釋放一次）",confirm_new},{"重複確認（無效）",confirm_new}};}
}
static void refresh(){
    auto& w=*live;std::ostringstream o;
    o<<"v5 可丟棄原型（模擬）  "<<guide<<"\r\n下一步："<<(step_i<steps.size()?std::to_string(step_i+1)+"/"+std::to_string(steps.size())+" "+steps[step_i].text:std::string("（引導已結束，可自由操作）"))<<"\r\n"
     <<"接收者：主動 #"<<w.active_receiver<<"  舊 host #"<<r1<<(w.receivers.count(r1)?"":"(已失效)")<<"  新 host #"<<r2<<"（同名、不同身分）\r\n";
    bool any=false;
    for(auto& h:w.handoffs){if(!h.id)continue;any=true;Assignment* a=h.cur();
        o<<"交接 #"<<h.id<<" "<<name(h.phase)<<" 持有額度 "<<h.credits<<(h.release_safe?"":"【確認後不安全失敗：無限期保留】")<<"  目前指派 #"<<h.current
         <<" 接收者 #"<<(a?a->receiver:0)<<" 確認資格="<<(a&&a->authority?"有":"無（已撤銷或失效）")<<"\r\n"
         <<"  改派要求 #"<<h.req.id<<" "<<name(h.req.s)<<" 候選 #"<<h.req.candidate<<"  舊接管工作已結束="<<(h.old_work_ended()?"是":"否")<<"  列管舊工作：";
        for(auto& x:h.asg)if(x.id&&x.id!=h.current)o<<"指派#"<<x.id<<(x.callback_active?" 回呼中":"")<<" 工作 "<<x.work<<"  ";o<<"\r\n";}
    if(!any)o<<"目前沒有未解決的交接\r\n";
    o<<"額度 "<<w.notice_used<<"/"<<World::notice_cap<<"（執行中 "<<w.running()<<" ＋ 交接保留 "<<w.held_credits()<<"）  已釋放 "<<w.c.released<<" 次  確認 "<<w.c.confirmed
     <<"  確認被拒 "<<w.c.confirm_rejected<<"  確認後失敗 "<<w.c.failed_after_ack<<"  交接失敗 "<<w.c.handoff_failed<<"\r\n不可用環境：";
    for(auto& [env,id]:w.current){Instance* i=w.find(id);if(i&&i->state!=InstanceState::Running)o<<env<<"(世代"<<i->gen<<") ";}
    SetWindowTextW(status_box,widen(o.str()).c_str());
    SendMessageW(list_box,LB_RESETCONTENT,0,0);for(auto& l:w.log)SendMessageW(list_box,LB_ADDSTRING,0,(LPARAM)widen(l).c_str());
    SendMessageW(list_box,LB_SETTOPINDEX,w.log.empty()?0:w.log.size()-1,0);
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_COMMAND&&live){auto& w=*live;const int id=LOWORD(wp);
        if(id>=1&&id<=6)reset(id);
        else if(id==20){if(step_i<steps.size()){w.L("—— 步驟 "+std::to_string(step_i+1)+"："+steps[step_i].text);steps[step_i].act(w);++step_i;}}
        else switch(id){case 30:fault_a(w);break;case 31:deliver(w);break;case 32:confirm_new(w);break;case 33:confirm_old(w);break;
            case 34:end_old(w);break;case 35:do_reassign(w);break;case 36:do_cancel(w);break;case 37:do_activate(w);break;}
        refresh();return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,wp,lp);
}
static int interactive(){
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"SafeHandoffPrototypeV4";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    HWND win=CreateWindowExW(0,wc.lpszClassName,L"v5 可丟棄原型：安全宿主交接與明確確認（模擬）",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1180,780,nullptr,nullptr,wc.hInstance,nullptr);
    const wchar_t* labels[]={L"引導1 延後確認",L"引導2 回呼內確認",L"引導3 改派",L"引導4 取消先",L"引導5 生效先",L"引導6 確認後失敗",L"▶ 下一步",
                             L"A 回呼故障",L"遞送",L"最新憑證確認",L"最舊憑證確認",L"舊工作結束",L"受理改派",L"取消改派",L"改派生效"};
    int ids[]={1,2,3,4,5,6,20,30,31,32,33,34,35,36,37};
    for(int i=0;i<15;++i){int r=i<7?0:1,c=i<7?i:i-7;CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,10+c*144,10+r*36,138,30,win,(HMENU)(INT_PTR)ids[i],wc.hInstance,nullptr);}
    status_box=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,10,86,1140,190,win,nullptr,wc.hInstance,nullptr);
    list_box=CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_BORDER|LBS_NOINTEGRALHEIGHT,10,282,1140,440,win,nullptr,wc.hInstance,nullptr);
    reset(1);refresh();
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    live.reset();return 0;
}

int resume_scenarios_main();int resume_interactive();
int main(int argc,char** argv){
    SetConsoleOutputCP(CP_UTF8);std::string mode=argc>1?argv[1]:"";
    if(mode=="--scenarios"){const char* log_name="scenarios-v5.log";std::ofstream f(log_name,std::ios::binary);try{scenarios(f);}catch(const std::exception& e){f<<"FAIL exception "<<e.what()<<'\n';++failures;}
        std::cout<<"scenarios failures="<<failures<<" invariant_checks="<<inv_checks<<" -> "<<log_name<<"\n";return failures?1:0;}
    if(mode=="--interactive")return interactive();
    if(mode=="--resume-scenarios")return resume_scenarios_main();   // companion pause/resume model (resume_main.cpp)
    if(mode=="--resume-interactive")return resume_interactive();
    std::cerr<<"use --scenarios | --interactive | --resume-scenarios | --resume-interactive\n";return 2;
}
