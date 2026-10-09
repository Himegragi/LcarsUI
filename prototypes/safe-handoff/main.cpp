// THROWAWAY safe-handoff prototype shell: --scenarios (self-checking) and --interactive (Win32). Logic in handoff_model.hpp.
#include "handoff_model.hpp"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>

using namespace handoff;

static int failures=0;
static void check(bool ok,const std::string& what,std::ostream& out){out<<(ok?"PASS ":"FAIL ")<<what<<'\n';if(!ok)++failures;}
static void dump(World& w,std::ostream& out){for(auto& l:w.log)out<<"  "<<l<<'\n';w.log.clear();}
static auto thrower=[]{throw std::runtime_error("extension callback threw");};
static FaultSpec unknown_shared(const std::string& svc){FaultSpec s;s.service=svc;s.reason="shared state of "+svc+" unknown";return s;}

static void scenarios(std::ostream& out){
    out<<"安全交接原型情境（模擬；交接安全與收尾安全是 fixture 宣告，不是偵測結果）。\n";
    {   out<<"\n== 1 基本安全交接：A 故障，B 獨立 ==\n";World w;int calls=0;std::vector<std::string> seen;
        w.host=[&](const Notification& n){++calls;seen=n.envs;return HostReply::KeepProcess;};
        w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);
        auto r=w.dispatch("A",thrower,unknown_shared("svcA"));
        check(r==ActionResult::Failed&&w.cur("A")->state==InstanceState::Unavailable,"A 回呼例外被攔截 → A 不可用",out);
        check(w.set_value("A",1)==ActionResult::RefusedUnavailable&&w.rebuild_target("A")==ActionResult::RefusedUnavailable,"A 的普通路徑立即拒絕",out);
        Token t;check(w.start_op("A",t)==ActionResult::RefusedUnavailable&&w.ledger->used()==0,"A 不能啟動新工作，不預留槽位",out);
        check(w.set_value("B",5)==ActionResult::Ok&&w.cur("B")->value==5&&w.cur("B")->state==InstanceState::Running,"B 獨立，繼續運作",out);
        check(calls==0&&w.pending.size()==1,"通知只排入佇列，回呼中不呼叫宿主",out);
        w.deliver();dump(w,out);
        check(calls==1&&seen==std::vector<std::string>{"A"}&&!w.process_exit_requested,"宿主收到一則通知（只含 A），選擇保留程序",out);
        check(w.cur("A")->state==InstanceState::Unavailable,"宿主回覆不等於恢復：A 仍不可用",out);
        w.dispatch("A",thrower,unknown_shared("svcA"));w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();
        check(calls==1&&w.c.repeated_fault_attempts==2,"同一故障世代的重複嘗試不產生通知風暴",out);
    }
    {   out<<"\n== 2 共享服務故障：A＋B 都不可用 ==\n";World w;int calls=0;Notification got{};
        w.host=[&](const Notification& n){++calls;got=n;return HostReply::KeepProcess;};
        w.add_env("A",{"shared"},false);w.add_env("B",{"shared"},false);w.add_env("C",{"own"},true);
        w.dispatch("A",thrower,unknown_shared("shared"));w.deliver();dump(w,out);
        check(w.cur("A")->state==InstanceState::Unavailable&&w.cur("B")->state==InstanceState::Unavailable,"共用 shared 的 A、B 都不可用",out);
        check(w.cur("C")->state==InstanceState::Running&&w.set_value("C",1)==ActionResult::Ok,"獨立的 C 繼續",out);
        check(calls==1&&got.envs.size()==2,"一則通知涵蓋 A、B",out);
        World u;u.add_env("A",{"x"},false);u.add_env("B",{"y"},false);u.add_env("C",{"z"},true);
        FaultSpec unk;unk.dependency_known=false;u.dispatch("A",thrower,unk);
        check(u.cur("B")->state==InstanceState::Unavailable&&u.cur("C")->state==InstanceState::Running,"相依未知：擴大到所有未宣告隔離的環境（B），宣告隔離的 C 繼續",out);
    }
    {   out<<"\n== 3 巢狀遍歷中故障：通知與收尾延後 ==\n";World w;int calls=0;w.host=[&](const Notification&){++calls;return HostReply::KeepProcess;};
        w.add_env("A",{"svcA"},false);
        w.traversals=1;w.dispatch("A",thrower,unknown_shared("svcA"));
        check(w.cur("A")->state==InstanceState::Unavailable&&w.set_value("A",1)==ActionResult::RefusedUnavailable,"遍歷中：A 立即不可用、拒絕操作",out);
        check(w.deliver()==0&&calls==0&&w.c.deferred_delivery==1,"遍歷仍在：通知延後，不在遍歷中呼叫宿主",out);
        w.traversals=0;w.deliver();dump(w,out);check(calls==1,"遍歷結束後才送出通知",out);
    }
    {   out<<"\n== 4 宿主回呼內重入 ==\n";World w;ActionResult in_action{},in_dispatch{};int inner_deliver=-1;
        w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);
        w.host=[&](const Notification&){in_action=w.set_value("A",1);in_dispatch=w.dispatch("B",[]{},{});inner_deliver=w.deliver();return HostReply::KeepProcess;};
        w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();dump(w,out);
        check(in_action==ActionResult::RefusedInHostCallback&&in_dispatch==ActionResult::RefusedInHostCallback&&inner_deliver==0,"回呼內的 A 操作、派送重入、遞送重入都被拒絕，不遞迴、不死結",out);
        check(w.c.delivered==1&&w.set_value("B",2)==ActionResult::Ok,"回呼結束後 B 可正常操作",out);
    }
    for(DiagMode m:{DiagMode::Disabled,DiagMode::Full,DiagMode::Throwing}){
        World w;int calls=0;w.host=[&](const Notification&){++calls;return HostReply::KeepProcess;};w.diag_mode=m;
        if(m==DiagMode::Full)for(int i=0;i<4;++i)w.diag.push_back("x");
        w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();
        out<<"\n== 5 診斷接收器 "<<(m==DiagMode::Disabled?"停用":m==DiagMode::Full?"已滿":"拋例外")<<" ==\n";dump(w,out);
        check(w.cur("A")->state==InstanceState::Unavailable&&calls==1,"必要故障狀態與宿主交接不受診斷影響",out);
        if(m==DiagMode::Throwing)check(w.diag_disabled_after_throw&&w.c.diag_failures==1,"拋例外的接收器被停用，只算一次失敗",out);
    }
    {   out<<"\n== 6 宿主選擇：保留程序 vs 模擬退出 ==\n";
        for(HostReply r:{HostReply::KeepProcess,HostReply::RequestExit}){World w;w.host=[&](const Notification&){return r;};
            w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();dump(w,out);
            check(w.process_exit_requested==(r==HostReply::RequestExit)&&w.cur("A")->state==InstanceState::Unavailable,
                  r==HostReply::KeepProcess?"保留程序：A 仍不可用":"模擬退出：只記錄，不實際退出；A 仍不可用",out);}
    }
    {   out<<"\n== 7 晚到結果：舊 A 不可用時、A 重建後 ==\n";World w;w.add_env("A",{"svcA"},false);
        Token t1,t2;w.start_op("A",t1);w.start_op("A",t2);const auto old_id=w.cur("A")->id;
        w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();
        w.ledger->publish(t1,42);w.settle();
        check(w.find(old_id)->value==0&&w.c.late_rejected_unavailable==1&&w.find(old_id)->reservations==1,"舊 A 不可用：晚到結果不更新 UI，只結清 1 個預留",out);
        check(!w.rebuild("A",{}).has_value(),"共享服務仍不可信：拒絕重建",out);
        w.replace_service("svcA");auto nid=w.rebuild("A",{});
        check(nid&&*nid!=old_id&&w.cur("A")->gen==2&&w.cur("A")->state==InstanceState::Running,"明確替換服務後重建：新實例、新世代",out);
        check(w.find(old_id)&&w.find(old_id)->state==InstanceState::Unavailable,"舊實例仍不可用且仍在等待收尾（未被重設成執行中）",out);
        w.ledger->publish(t2,99);w.settle();dump(w,out);
        check(w.cur("A")->value==0&&!w.find(old_id)&&w.c.retired==1,"重建後才到的舊結果不更新新實例；舊實例結清後退休",out);
        check(w.ledger->used()==0&&w.ledger->released==2,"兩個槽位各歸還一次",out);
    }
    {   out<<"\n== 8 重複／過期／外來結果；目標世代（實例仍執行中）==\n";World w;w.add_env("A",{"svcA"},false);World other;other.add_env("A",{"s"},false);
        Token t;w.start_op("A",t);
        check(w.ledger->publish(t,1)&&!w.ledger->publish(t,1),"重複發布被拒絕",out);
        Token foreign;other.start_op("A",foreign);
        check(!w.ledger->publish(foreign,1)&&!w.ledger->publish({w.ledger->id,t.instance,99,1},1),"外來帳本、越界槽位都被拒絕（錯誤預留世代見情境 17）",out);
        w.settle();check(w.c.late_applied==1&&w.cur("A")->value==1,"有效結果套用",out);
        check(!w.ledger->publish(t,5),"已結算的 token 過期",out);
        Token g;w.start_op("A",g);w.rebuild_target("A");w.ledger->publish(g,7);w.settle();dump(w,out);
        check(w.cur("A")->state==InstanceState::Running&&w.cur("A")->value==0&&w.c.late_rejected_generation==1&&w.c.late_rejected_unavailable==0,
              "實例仍執行中：只因目標世代不符而拒絕（與不可用拒絕分開計數）",out);
    }
    {   out<<"\n== 9 重建被拒：條件不成立、容量已滿 ==\n";World w;w.add_env("A",{"svcA"},false);Token t;w.start_op("A",t);
        w.dispatch("A",thrower,unknown_shared("svcA"));w.replace_service("svcA");
        check(!w.rebuild("A",{false,true})&&!w.rebuild("A",{true,false}),"隔離或資源條件不成立：拒絕",out);
        World full;full.add_env("A",{"s"},false);full.add_env("B",{"b"},true);full.add_env("C",{"c"},true);full.add_env("D",{"d"},true);
        Token ft;full.start_op("A",ft);full.dispatch("A",thrower,unknown_shared("s"));full.replace_service("s");
        check(!full.rebuild("A",{})&&full.c.rebuilds_refused==1,"實例容量已滿（舊 A 收尾未完成）：拒絕重建",out);
        dump(full,out);
    }
    {   out<<"\n== 10 有界重複故障 ==\n";World w;int calls=0;w.host=[&](const Notification&){++calls;return HostReply::KeepProcess;};w.add_env("A",{"svcA"},false);
        w.dispatch("A",thrower,unknown_shared("svcA"));for(int i=0;i<1000;++i)w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();
        check(calls==1&&w.pending.empty()&&w.log.size()<=World::log_cap&&w.diag.size()<=World::diag_cap,"1000 次重複嘗試：1 則通知；日誌與診斷都有上限",out);
        w.log.clear();
    }
    {   out<<"\n== 11 宿主通知回呼拋例外 ==\n";World w;int calls=0;w.host=[&](const Notification&)->HostReply{++calls;throw std::runtime_error("host failed");};
        w.add_env("A",{"svcA"},false);w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();w.deliver();dump(w,out);
        check(calls==1&&w.c.handoff_failed==1&&w.c.delivered==0&&w.pending.empty(),"只在宣告的宿主邊界攔截；回報交接失敗，不重試、不視為已確認",out);
        check(w.cur("A")->state==InstanceState::Unavailable,"A 仍不可用",out);
    }
    {   out<<"\n== 12 未宣告安全交接路徑 ==\n";World w;int calls=0;w.host=[&](const Notification&){++calls;return HostReply::KeepProcess;};w.add_env("A",{"svcA"},false);
        FaultSpec s=unknown_shared("svcA");s.handoff_path_safe=false;w.dispatch("A",thrower,s);w.deliver();dump(w,out);
        check(w.out_of_scope&&calls==0&&w.set_value("A",1)==ActionResult::RefusedOutOfScope&&!w.rebuild("A",{}),"標記超出範圍：不通知、不宣稱恢復、不重建（致命路徑不在本原型）",out);
    }
    {   out<<"\n== 13 回呼例外但宣告狀態仍有效 ==\n";World w;w.add_env("A",{"svcA"},false);FaultSpec s;s.state_still_valid=true;
        check(w.dispatch("A",thrower,s)==ActionResult::Failed&&w.cur("A")->state==InstanceState::Running&&w.pending.empty(),"只回報失敗結果，不停止範圍",out);
    }
    {   out<<"\n== 14 真實 std::thread 晚到結果 ==\n";World w;w.add_env("A",{"svcA"},false);Token t;w.start_op("A",t);const auto old_id=w.cur("A")->id;
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
    // ---- review regressions ----
    {   out<<"\n== 15 宿主回呼內的變更入口、非擁有者執行緒 ==\n";World w;w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);
        Token t;w.start_op("A",t);w.ledger->publish(t,5);
        size_t inst_in_cb=0,used_in_cb=0;int settled_in_cb=-1;bool rebuilt_in_cb=false,added_in_cb=false;
        w.host=[&](const Notification&){
            w.replace_service("svcA");rebuilt_in_cb=w.rebuild("A",{}).has_value();settled_in_cb=w.settle();
            added_in_cb=w.add_env("Z",{"z"},true)==ActionResult::Ok;w.service_fault("svcB");
            inst_in_cb=w.instances.size();used_in_cb=w.ledger->used();return HostReply::KeepProcess;};
        w.dispatch("A",thrower,unknown_shared("svcA"));w.deliver();dump(w,out);
        check(!rebuilt_in_cb&&!added_in_cb&&settled_in_cb==0&&inst_in_cb==2&&used_in_cb==1,"回呼內重建／新增環境／結算都被拒絕：實例數、槽位不變",out);
        check(!w.services["svcA"].trustworthy&&w.cur("B")->state==InstanceState::Running&&w.cur("A")->state==InstanceState::Unavailable,"回呼內替換服務、宣告服務故障都沒有生效",out);
        World o;o.add_env("A",{"svcA"},false);int before=o.cur("A")->value;ActionResult r{};
        std::thread off([&]{r=o.set_value("A",7);});off.join();
        check(r==ActionResult::RefusedWrongThread&&o.cur("A")->value==before&&o.c.refused_actions==0,"非擁有者執行緒的設值被拒絕，UI 狀態與計數都不變",out);
        World s;s.add_env("A",{"svcA"},false);s.traversals=1;const auto svc_gen=s.services["svcA"].gen;
        check(s.replace_service("svcA")==ActionResult::RefusedNotSafePoint&&s.services["svcA"].gen==svc_gen&&s.add_env("Q",{"q"},true)==ActionResult::RefusedNotSafePoint&&s.instances.size()==1,
              "遍歷進行中：替換服務、新增環境被拒絕（fixture 規則：非安全點直接拒絕）",out);
    }
    {   out<<"\n== 16 故障→替換→重建→退休循環超過通知容量（不遞送）==\n";World w;int calls=0;std::vector<unsigned> gens;
        w.host=[&](const Notification& n){++calls;for(auto g:n.gens)gens.push_back(g);return HostReply::KeepProcess;};
        w.add_env("A",{"svcA"},false);int accepted=0,refused=0;size_t max_pending=0;bool threw=false;
        try{for(int cycle=0;cycle<8;++cycle){
            if(w.dispatch("A",thrower,unknown_shared("svcA"))==ActionResult::Failed&&w.cur("A")->state==InstanceState::Unavailable)++accepted;
            w.replace_service("svcA");if(w.rebuild("A",{}));else ++refused;w.settle();max_pending=std::max(max_pending,w.pending.size());}}
        catch(const std::exception& e){threw=true;out<<"例外："<<e.what()<<"\n";}
        out<<"受理的故障轉換="<<accepted<<" 拒絕重建="<<refused<<" 最大待送通知="<<max_pending<<"\n";
        check(!threw,"循環中沒有意外例外",out);
        check(max_pending<=World::instance_cap&&refused>0,"待送通知不超過容量；容量用盡時重建被明確拒絕",out);
        const int pending_before=(int)w.pending.size();w.deliver();
        check(calls==pending_before&&calls==accepted,"每個受理的故障轉換各有一次宿主回呼",out);
        out<<"遞送的通知快照世代：";for(auto g:gens)out<<g<<' ';out<<"（已退休 "<<w.c.retired<<" 個舊實例）\n";
        check(gens==std::vector<unsigned>{1,2,3,4}&&w.c.retired>=3,"已退休世代的通知仍以快照送達，各世代一則，不合併",out);
        check(w.notice_used==0&&w.pending.empty(),"遞送後通知容量完全回收（目前的 A 已不可用，不持有額度）",out);
        w.replace_service("svcA");check(w.rebuild("A",{}).has_value(),"遞送後容量回收，重建成功",out);dump(w,out);
    }
    {   out<<"\n== 17 錯誤預留世代打到仍在預留中的槽位 ==\n";World w;w.add_env("A",{"svcA"},false);Token t;w.start_op("A",t);
        Token bad=t;bad.gen=t.gen+1;
        check(!w.ledger->publish(bad,1)&&w.ledger->used()==1&&w.ledger->published==0,"錯誤世代被拒絕，槽位仍是預留中、未發布",out);
        check(w.ledger->publish(t,2),"之後有效發布成功",out);w.settle();check(w.cur("A")->value==2,"有效結果套用",out);
    }
    {   out<<"\n== 18 未宣告安全交接後，已預留結果不得更新 UI ==\n";World w;w.add_env("A",{"svcA"},false);Token t;w.start_op("A",t);
        FaultSpec s=unknown_shared("svcA");s.handoff_path_safe=false;w.dispatch("A",thrower,s);
        w.ledger->publish(t,9);w.settle();dump(w,out);
        check(w.cur("A")->value==0&&w.ledger->used()==1&&w.c.held_out_of_scope==1,"超出範圍：結果不套用、預留保留（不宣稱已安全收尾）",out);
        check(w.set_value("A",3)==ActionResult::RefusedOutOfScope&&w.cur("A")->value==0,"超出範圍後普通操作也被拒絕；此狀態刻意未解決",out);
    }
    {   out<<"\n== 19 已有待送通知後才進入未宣告安全交接 ==\n";World w;int calls=0;w.host=[&](const Notification&){++calls;return HostReply::KeepProcess;};
        w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);Token ta;w.start_op("A",ta);
        w.dispatch("A",thrower,unknown_shared("svcA"));   // A: pending safe notice, old A awaiting one result
        FaultSpec unsafe=unknown_shared("svcB");unsafe.handoff_path_safe=false;w.dispatch("B",thrower,unsafe);
        const size_t pending=w.pending.size(),used=w.notice_used,instances=w.instances.size();
        w.ledger->publish(ta,5);const int n=w.deliver();w.settle();w.cleanup();dump(w,out);
        check(calls==0&&n==0&&w.c.delivered==0,"超出範圍：不再呼叫宿主（既有待送通知不送出）",out);
        check(w.pending.size()==pending&&w.notice_used==used,"待送通知與通知額度保持原狀",out);
        check(w.ledger->published_waiting()==1&&w.instances.size()==instances,"已發布結果保持原狀，舊實例不退休",out);
    }
    {   out<<"\n== 20 沒有登記宿主接收者 ==\n";World w;w.add_env("A",{"svcA"},false);
        w.dispatch("A",thrower,unknown_shared("svcA"));const int n=w.deliver();dump(w,out);
        check(w.c.delivered==0&&w.c.handoff_failed==1&&!w.process_exit_requested,"沒有接收者：記錄交接失敗，不算已遞送、不默認保留程序",out);
        check(n==1&&w.pending.empty()&&w.notice_used==0,"fixture 政策：與回呼拋例外相同，該則通知視為已嘗試並歸還額度，不自動重試",out);
        check(w.cur("A")->state==InstanceState::Unavailable,"A 仍不可用",out);
    }
    {   out<<"\n== 21 重複的環境名稱 ==\n";World w;w.add_env("A",{"svcA"},false);Token t;w.start_op("A",t);
        const auto id=w.cur("A")->id;const size_t used=w.notice_used,inst=w.instances.size(),slots=w.ledger->used();
        const auto r=w.add_env("A",{"other"},true);
        check(r==ActionResult::RefusedDuplicate,"重複名稱被明確拒絕（RefusedDuplicate）",out);
        check(w.current["A"]==id&&w.cur("A")->state==InstanceState::Running&&w.notice_used==used&&w.instances.size()==inst&&w.ledger->used()==slots&&!w.services.count("other"),
              "原實例身分、狀態、額度、current 對應、服務表都不變",out);
    }
    out<<"\nscenarios "<<(failures?"FAIL":"PASS")<<" failures="<<failures<<'\n';
}

// ---------- interactive Win32 (simulated; buttons call the model) ----------
static std::unique_ptr<World> live;static HWND status_box,list_box;static Token tokA[2];static HostReply host_choice=HostReply::KeepProcess;static std::string guide;
static std::wstring widen(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n);return w;}
// Guided walkthrough: ordered steps, each a call into the model (no duplicated logic).
struct Step{const char* text;std::function<void(World&)> act;};
static std::vector<Step> steps;static size_t step_i=0;
static std::string svcA(World& w){return w.cur("A")?w.cur("A")->services[0]:"svcA";}
static void publish_one(World& w){for(auto& t:tokA)if(w.ledger->publish(t,77)){w.L("送達一個舊 A 結果（worker 端只有 token）");break;}w.settle();}
static void try_ab(World& w){w.L(std::string("A 設值：")+name(w.set_value("A",1)));w.L(std::string("B 設值：")+name(w.set_value("B",1)));}
static void reset(int kind){
    live=std::make_unique<World>();auto& w=*live;host_choice=HostReply::KeepProcess;step_i=0;
    w.host=[](const Notification&){return host_choice;};
    if(kind==2){w.add_env("A",{"shared"},false);w.add_env("B",{"shared"},false);w.add_env("C",{"svcC"},true);guide="引導2：共享服務故障（A、B 共用 shared；C 獨立）";
        steps={{"宣告 shared 服務不可信",[](World& w){w.service_fault("shared");}},{"嘗試 A／B 操作（都被拒絕）",try_ab},
               {"C 仍可操作",[](World& w){w.L(std::string("C 設值：")+name(w.set_value("C",1)));}},{"遞送宿主通知（一則涵蓋 A、B）",[](World& w){w.deliver();}}};}
    else if(kind==3){w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);guide="引導3：重建與舊結果";
        steps={{"A 回呼故障",[](World& w){w.dispatch("A",thrower,unknown_shared(svcA(w)));}},{"遞送宿主通知",[](World& w){w.deliver();}},
               {"嘗試重建 A（服務仍不可信 → 拒絕）",[](World& w){w.rebuild("A",{});}},{"宿主明確替換 A 的服務",[](World& w){w.replace_service(svcA(w));}},
               {"重建 A（新實例、新世代）",[](World& w){w.rebuild("A",{});}},{"送達舊 A 結果（不更新新 A）",publish_one},{"新 A 可操作",try_ab}};}
    else{w.add_env("A",{"svcA"},false);w.add_env("B",{"svcB"},true);guide="引導1：故障→安全通知→宿主保留→晚到結果";
        steps={{"A 回呼故障",[](World& w){w.dispatch("A",thrower,unknown_shared(svcA(w)));}},{"嘗試 A／B 操作（A 拒絕、B 成功）",try_ab},
               {"遞送宿主通知（宿主選擇：保留程序）",[](World& w){w.deliver();}},{"送達舊 A 結果（不更新、只結清）",publish_one}};}
    w.start_op("A",tokA[0]);w.start_op("A",tokA[1]);
}
static void refresh(){
    auto& w=*live;std::ostringstream o;
    o<<"可丟棄安全交接原型（模擬；不會真的退出程序）  "<<guide<<"\r\n"
     <<"下一步："<<(step_i<steps.size()?std::to_string(step_i+1)+"/"+std::to_string(steps.size())+" "+steps[step_i].text:std::string("（引導已結束，可自由操作）"))<<"\r\n"
     <<"宣告（fixture 輸入，不是偵測）：安全交接路徑="<<(w.out_of_scope?"否":"是")<<"  收尾安全=是  宿主下次選擇="
     <<(host_choice==HostReply::KeepProcess?"保留程序":"請求退出")<<"  目前："<<(w.process_exit_requested?"已請求退出（模擬）":"程序保留")<<"\r\n";
    for(auto& [env,id]:w.current){Instance* i=w.find(id);if(!i)continue;
        o<<env<<"：世代"<<i->gen<<" "<<(i->state==InstanceState::Running?"執行中":"不可用")<<" 值="<<i->value<<" 目標世代="<<i->target_gen<<" 未結工作="<<i->reservations<<" 服務=";
        for(auto& s:i->services)o<<s<<(w.services[s].trustworthy?"(可信)":"(不可信)")<<" ";o<<"\r\n";}
    o<<"等待收尾的舊實例 "<<(w.instances.size()-w.current.size())<<"  待送通知 "<<w.pending.size()<<"  已送 "<<w.c.delivered<<"  交接失敗 "<<w.c.handoff_failed
     <<"  通知額度 "<<w.notice_used<<"/"<<World::notice_cap<<(w.out_of_scope?"  【超出安全交接範圍：結果與預留保持原狀】":"")<<"\r\n"
     <<"晚到結果 套用 "<<w.c.late_applied<<" 拒絕(不可用) "<<w.c.late_rejected_unavailable<<" 拒絕(世代) "<<w.c.late_rejected_generation<<" 拒絕(已退休) "<<w.c.late_rejected_missing
     <<"  拒絕發布 "<<w.ledger->rejected_publish<<"  槽位使用 "<<w.ledger->used()<<"/"<<w.ledger->slots.size()<<"  拒絕操作 "<<w.c.refused_actions<<"  重建 "<<w.c.rebuilds<<" 拒絕重建 "<<w.c.rebuilds_refused;
    SetWindowTextW(status_box,widen(o.str()).c_str());
    SendMessageW(list_box,LB_RESETCONTENT,0,0);for(auto& l:w.log)SendMessageW(list_box,LB_ADDSTRING,0,(LPARAM)widen(l).c_str());
    SendMessageW(list_box,LB_SETTOPINDEX,w.log.empty()?0:w.log.size()-1,0);
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_COMMAND&&live){auto& w=*live;
        switch(LOWORD(wp)){
        case 1:case 2:case 3:reset(LOWORD(wp));break;
        case 20:if(step_i<steps.size()){w.L("—— 步驟 "+std::to_string(step_i+1)+"："+steps[step_i].text);steps[step_i].act(w);++step_i;}break;
        case 10:w.dispatch("A",thrower,unknown_shared(svcA(w)));break;
        case 11:w.L(std::string("宣告 shared 不可信：")+name(w.service_fault("shared")));break;
        case 12:w.deliver();break;
        case 13:host_choice=host_choice==HostReply::KeepProcess?HostReply::RequestExit:HostReply::KeepProcess;
                w.L(host_choice==HostReply::KeepProcess?"宿主下次選擇：保留程序":"宿主下次選擇：請求退出（模擬）");break;
        case 14:publish_one(w);break;
        case 15:w.L(std::string("替換服務：")+name(w.replace_service(svcA(w))));break;
        case 16:w.rebuild("A",{});break;
        case 17:try_ab(w);break;
        }
        refresh();return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,wp,lp);
}
static int interactive(){
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"SafeHandoffPrototype";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    HWND win=CreateWindowExW(0,wc.lpszClassName,L"可丟棄原型：安全宿主交接（模擬，不實際退出程序）",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1100,760,nullptr,nullptr,wc.hInstance,nullptr);
    const wchar_t* labels[]={L"引導1 重設",L"引導2 共享",L"引導3 重建",L"▶ 下一步",L"A 回呼故障",L"shared 服務故障",
                             L"遞送宿主通知",L"切換宿主下次選擇",L"送達舊A結果",L"替換A服務",L"重建A",L"嘗試 A/B 操作"};
    int ids[]={1,2,3,20,10,11,12,13,14,15,16,17};
    for(int i=0;i<12;++i){int r=i<6?0:1,c=i<6?i:i-6;CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,10+c*176,10+r*36,168,30,win,(HMENU)(INT_PTR)ids[i],wc.hInstance,nullptr);}
    status_box=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,10,86,1060,170,win,nullptr,wc.hInstance,nullptr);
    list_box=CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_BORDER|LBS_NOINTEGRALHEIGHT,10,262,1060,440,win,nullptr,wc.hInstance,nullptr);
    reset(1);refresh();
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    live.reset();return 0;
}

int main(int argc,char** argv){
    SetConsoleOutputCP(CP_UTF8);std::string mode=argc>1?argv[1]:"";
    if(mode=="--scenarios"){std::ofstream f("scenarios-v3.log",std::ios::binary);try{scenarios(f);}catch(const std::exception& e){f<<"FAIL exception "<<e.what()<<'\n';++failures;}
        std::cout<<"scenarios failures="<<failures<<" -> scenarios-v3.log\n";return failures?1:0;}
    if(mode=="--interactive")return interactive();
    std::cerr<<"use --scenarios | --interactive\n";return 2;
}
