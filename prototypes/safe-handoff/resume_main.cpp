// THROWAWAY pause/resume companion v2: --resume-scenarios (self-checking) and --resume-interactive (Win32). Model: resume_model.hpp.
#include "resume_model.hpp"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>

using namespace resume;

static int fails=0,invs=0;
static void check(bool ok,const std::string& what,std::ostream& out){out<<(ok?"PASS ":"FAIL ")<<what<<'\n';if(!ok)++fails;}
static void I(World& w,std::ostream& out){++invs;if(!w.invariant()){++fails;out<<"FAIL 容量／處置不變式不成立 used="<<w.used()<<" reserved="<<w.c.reserved<<" freed="<<w.c.freed<<'\n';}}
static void dump(World& w,std::ostream& out){for(auto& l:w.log)out<<"  "<<l<<'\n';w.log.clear();}
static Slot slot_of(World& w,const Token& t){std::lock_guard l(w.ledger->m);return w.ledger->slots[t.slot];}
static std::string trace(World& w,const std::string& n){Env* e=w.env(n);Attempt* a=w.attempt_of(n);std::ostringstream o;
    o<<"    [軌跡] "<<n<<" 狀態="<<name(e->state)<<" 嘗試#"<<(e->attempt)<<" 固定本批="<<a->batch.size()<<" 游標="<<a->cursor
     <<" 本批外留存="<<(w.retained(w.idx(e))-(e->attempt?a->batch.size()-a->cursor:0))
     <<" 已套用="<<e->applied<<" 不適用="<<e->not_applicable<<" 值="<<e->value<<" 使用槽位="<<w.used()<<"/"<<World::slot_cap<<'\n';return o.str();}
static std::string full(World& w){std::ostringstream o;o<<w.used()<<'|'<<w.log.size()<<'|'<<w.routes.size()<<'|'<<w.ledger->rejected<<'|'<<w.ledger->published<<'|';
    const auto& c=w.c;   // explicit fields (no type punning / padding dependence)
    for(int v:{c.reserved,c.freed,c.applied,c.not_applicable,c.no_result,c.accepted_actions,c.started_from_queue,c.not_executed,c.refused,c.settled,
        c.attempts,c.invalidated,c.commits,c.double_apply,c.stale_items,c.normal_rounds})o<<v<<',';
    o<<w.receipt_credits<<'/'<<w.outbox.size()<<'/'<<w.route_seq<<'/'<<w.last_action<<';';
    for(auto& e:w.envs)o<<e.name<<(int)e.state<<e.route<<e.attempt<<e.value<<e.queue.size()<<e.applied<<e.not_applicable<<e.data_version<<e.target_gen<<';';
    for(auto& r:w.routes)o<<r.id<<r.alive<<r.version;return o.str();}

static void scenarios(std::ostream& out){
    out<<"暫停／恢復伴隨原型 v2 情境（v1 R1–R6 ＋ 審查 R7–R9）（模擬；與 v5 主機確認模型分開，不證明各子系統已整合）。\n";
    {   out<<"\n== R1 途徑失效只暫停 A；未開始動作回報未執行；取消請求與允許繼續；故障不能恢復 ==\n";
        World w;const auto ra=w.add_route("hostA"),rb=w.add_route("hostB");w.add_env("A",ra);w.add_env("B",rb);
        Token t1,t2,t3;w.start("A",false,t1);w.start("A",true,t2);w.start("B",false,t3);w.submit("A");w.submit("A");w.submit("B");I(w,out);
        w.lose_route(ra);I(w,out);
        check(w.env("A")->state==EnvState::Paused&&w.env("B")->state==EnvState::Running,"只有 A 暫停；B 有獨立途徑繼續",out);
        Token x;check(w.submit("A")==R::RefusedPaused&&w.start("A",false,x)==R::RefusedPaused,"A 的新普通命令被拒",out);
        check(w.submit("B")==R::Ok&&w.start("B",false,x)==R::Ok,"B 仍可受理與開始",out);
        check(w.env("A")->not_executed==2&&w.c.not_executed==2&&w.env("A")->queue.empty()&&w.env("B")->queue.size()==2,"A 的 2 個已受理未開始動作各回報一次未執行；B 的待辦不受影響",out);
        check(slot_of(w,t1).cancel_requested&&!slot_of(w,t2).cancel_requested&&!slot_of(w,t3).cancel_requested,"已開始：預設提出取消請求；宣告允許繼續者不取消；B 的工作不取消",out);
        check(w.count(0,Slot::Running)==2,"取消請求不是終態：A 的兩個預留仍保留",out);
        w.lose_route(ra);check(w.env("A")->not_executed==2,"重複失效不重複回報",out);
        w.ledger->publish(t1,0,true);w.ledger->publish(t2,77);w.drain();I(w,out);
        check(w.c.no_result==1&&w.retained(0)==1&&w.env("A")->value==0,"被取消的工作結清為無結果並歸還；完成結果留存、不套用 UI",out);
        check(slot_of(w,t2).status==Status::Completed,"命令終態可觀察（已完成），與 UI 處置分開",out);
        check(w.safe_cleanup("A")==R::Ok,"暫停中仍可安全收尾",out);
        World f;const auto fa=f.add_route("hostA");f.add_env("A",fa);f.lose_route(fa);const auto fa2=f.add_route("hostA");f.fault("A");
        check(!f.begin_resume("A",fa2)&&f.env("A")->state==EnvState::Faulted,"故障實例不能恢復（需另行重建）",out);
        const auto ra2=w.add_route("hostA");check(w.env("A")->state==EnvState::Paused,"只安裝新途徑：不自動恢復",out);
        check(w.begin_resume("A",ra2).has_value(),"狀態有效的暫停環境可明確請求恢復",out);I(w,out);dump(w,out);
    }
    {   out<<"\n== R2 暫停中完成多個工作；固定本批、逐份處理、全部處置後才提交 ==\n";
        World w;const auto ra=w.add_route("hostA");w.add_env("A",ra);Token t[4];for(auto& x:t)w.start("A",true,x);
        w.lose_route(ra);w.ledger->publish(t[0],10);w.ledger->publish(t[1],20);w.ledger->publish(t[2],30);w.drain();I(w,out);
        check(w.retained(0)==3&&w.env("A")->value==0,"3 份結果留存，UI 未變",out);
        const auto ra2=w.add_route("hostA");const auto a=w.begin_resume("A",ra2);out<<trace(w,"A");
        check(a&&w.attempt_of("A")->batch.size()==3,"開始恢復：固定本批 3 份",out);
        w.resume_step(*a,1);out<<trace(w,"A");I(w,out);
        check(w.env("A")->applied==1&&w.env("A")->value==10,"處理一份：套用次數 +1",out);
        Token y;check(w.commit_resume(*a)==R::RefusedNotDone&&w.submit("A")==R::RefusedPaused&&w.start("A",false,y)==R::RefusedPaused,"本批未處理完：不能提交；普通操作仍拒絕",out);
        w.resume_step(*a,1);w.resume_step(*a,1);out<<trace(w,"A");
        check(w.commit_resume(*a)==R::Ok&&w.env("A")->state==EnvState::Running&&w.env("A")->applied==3&&w.env("A")->value==30,"全部處置並通過最後檢查後才重新開放",out);I(w,out);dump(w,out);
    }
    {   out<<"\n== R3 恢復途中途徑失效／最後一份後提交前失效／同名替換 ==\n";
        {   World w;const auto ra=w.add_route("hostA");w.add_env("A",ra);Token t[3];for(auto& x:t)w.start("A",true,x);w.lose_route(ra);
            for(int i=0;i<3;++i)w.ledger->publish(t[i],i+1);w.drain();
            const auto ra2=w.add_route("hostA");const auto a=w.begin_resume("A",ra2);w.resume_step(*a,1);out<<trace(w,"A");
            check(w.env("A")->applied==1,"已套用 1 份",out);
            w.lose_route(ra2);I(w,out);out<<trace(w,"A");
            check(w.env("A")->state==EnvState::Paused&&w.resume_step(*a,1)==R::RefusedStale&&w.commit_resume(*a)==R::RefusedStale,"片段之間失效：嘗試失效、維持暫停；舊嘗試不能前進或提交",out);
            const auto ra3=w.add_route("hostA");check(w.env("A")->state==EnvState::Paused,"只重新安裝：不恢復執行中",out);
            const auto b=w.begin_resume("A",ra3);out<<trace(w,"A");
            check(b&&*b!=*a&&w.attempt_of("A")->batch.size()==2,"新的明確恢復：新快照只含尚未處理的 2 份",out);
            w.resume_step(*b,5);check(w.commit_resume(*b)==R::Ok,"完成並提交",out);
            check(w.env("A")->applied==3&&w.c.applied+w.c.not_applicable==3&&w.c.double_apply==0,"每份結果只有一個最終處置，沒有重跑",out);I(w,out);dump(w,out);}
        {   World w;const auto ra=w.add_route("hostA");w.add_env("A",ra);Token t[2];for(auto& x:t)w.start("A",true,x);w.lose_route(ra);
            w.ledger->publish(t[0],5);w.ledger->publish(t[1],6);w.drain();
            const auto ra2=w.add_route("hostA");const auto a=w.begin_resume("A",ra2);w.resume_step(*a,2);
            w.lose_route(ra2);out<<trace(w,"A");
            check(w.commit_resume(*a)==R::RefusedStale&&w.env("A")->state==EnvState::Paused,"最後一份後、提交前失效：拒絕重新開放",out);
            const auto ra3=w.add_route("hostA");const auto b=w.begin_resume("A",ra3);w.route(ra3)->alive=false;   // fixture injection: loss not yet propagated
            check(b&&w.attempt_of("A")->batch.empty()&&w.commit_resume(*b)==R::Invalidated&&w.env("A")->state==EnvState::Paused,"提交時的再檢查本身也會擋下（不只靠失效通知）",out);
            const auto ra4=w.add_route("hostA");const auto c2=w.begin_resume("A",ra4);
            check(c2&&w.commit_resume(*c2)==R::Ok&&w.env("A")->applied==2,"之後的新嘗試：已處置的結果不再處理",out);I(w,out);dump(w,out);}
        {   World w;const auto ra=w.add_route("host");w.add_env("A",ra);w.lose_route(ra);const auto rn=w.add_route("host");
            check(rn!=ra&&w.route(rn)->version==2&&w.route(rn)->name==w.route(ra)->name,"同名替換：新身分、新版本",out);
            check(!w.begin_resume("A",ra)&&w.begin_resume("A",rn).has_value(),"用舊身分恢復被拒；新身分可以",out);I(w,out);dump(w,out);}
    }
    {   out<<"\n== R4 恢復期間持續有新結果到達；保留滿載 ==\n";
        World w;const auto ra=w.add_route("hostA");w.add_env("A",ra);Token t[6];for(auto& x:t)w.start("A",true,x);Token z;
        check(w.start("A",true,z)==R::RefusedNoCapacity,"6 個槽位全被已受理工作占用：新工作在受理前被拒",out);
        w.lose_route(ra);w.ledger->publish(t[0],1);w.ledger->publish(t[1],2);w.drain();
        const auto ra2=w.add_route("hostA");const auto a=w.begin_resume("A",ra2);int k=2;
        while(w.attempt_of("A")->cursor<w.attempt_of("A")->batch.size()){w.ledger->publish(t[k],k+1);++k;w.drain();w.resume_step(*a,1);out<<trace(w,"A");
            check(w.attempt_of("A")->batch.size()==2,"本批大小固定為 2（新到結果不加入）",out);I(w,out);}
        w.ledger->publish(t[k],k+1);++k;w.drain();
        const size_t late=w.retained(0);out<<"  提交前晚到留存="<<late<<'\n';
        check(w.commit_resume(*a)==R::Ok&&late==3,"仍有 3 份晚到結果時也能提交（不要求全部排空）",out);I(w,out);
        int rounds=0;while(w.retained(0)||w.count(0,Slot::Running)){if(k<6){w.ledger->publish(t[k],k+1);++k;w.drain();}w.normal_round(1);++rounds;I(w,out);if(rounds>20)break;}
        out<<"  一般處理輪="<<rounds<<'\n';
        check(w.c.applied+w.c.not_applicable==6&&w.used()==0&&rounds<=6,"有限生產者：之後的有限一般輪處理完所有晚到結果，容量全數歸還",out);
        World p;const auto pa=p.add_route("hostA");p.add_env("A",pa);Token q[6];for(auto& x:q)p.start("A",true,x);p.lose_route(pa);for(int i=0;i<6;++i)p.ledger->publish(q[i],i);p.drain();
        check(p.retained(0)==6&&p.used()==6,"保留滿載：6 份已完成結果全部留存、沒有遺失",out);
        const auto pa2=p.add_route("hostA");const auto pb=p.begin_resume("A",pa2);
        check(pb.has_value(),"滿載時仍可開始恢復（本批儲存已預先保留）",out);p.resume_step(*pb,3);p.resume_step(*pb,3);
        check(p.commit_resume(*pb)==R::Ok&&p.used()==0&&p.c.applied==6,"全部處置後歸還",out);I(p,out);w.log.clear();p.log.clear();
        out<<"  說明：有限生產者只顯示有界進度，不是普遍公平或硬延遲保證。\n";
    }
    {   out<<"\n== R5 版本過期 → 不適用；重複恢復／片段／提交；重複發布與槽位重用 ==\n";
        World w;const auto ra=w.add_route("hostA");w.add_env("A",ra);Token t1,t2,t3,t4;
        w.start("A",true,t1);w.external_change("A");w.start("A",true,t2);w.start("A",true,t3);w.start("A",false,t4);w.submit("A");
        w.lose_route(ra);w.ledger->publish(t1,11);w.ledger->publish(t2,22);w.ledger->publish(t3,33);w.drain();I(w,out);
        check(w.retained(0)==3&&w.count(0,Slot::Running)==1&&slot_of(w,t4).cancel_requested&&w.env("A")->not_executed==1,"三種所有權分開：留存結果 3、進行中工作 1（已請求取消）、未開始動作 1（未執行）",out);
        const auto ra2=w.add_route("hostA");const auto a=w.begin_resume("A",ra2);
        check(!w.begin_resume("A",ra2)&&w.env("A")->attempt==*a,"重複請求恢復：不另開嘗試",out);
        w.resume_step(*a,1);check(w.env("A")->not_applicable==1&&w.env("A")->value==0,"開始前的資料版本已變：不適用（不是成功套用）",out);
        w.resume_step(*a,1);check(w.env("A")->applied==1&&w.env("A")->value==22,"目前有效的結果：套用",out);
        w.external_change("A");w.resume_step(*a,1);
        check(w.env("A")->not_applicable==2&&w.env("A")->value==22,"快照後、套用前資料變更：不適用",out);out<<trace(w,"A");
        check(w.resume_step(*a,1)==R::Ok&&w.env("A")->applied==1&&w.env("A")->not_applicable==2,"重複片段：沒有變化",out);
        check(w.commit_resume(*a)==R::Ok&&w.commit_resume(*a)==R::RefusedStale,"重複提交被拒",out);
        const int freed=w.c.freed;check(!w.ledger->publish(t2,99)&&w.c.freed==freed&&w.env("A")->value==22,"已處置結果的重複發布：被拒、不重套、不重複釋放",out);
        Token t5;w.start("A",true,t5);
        check(t5.slot==t1.slot&&t5.gen!=t1.gen&&!w.ledger->publish(t1,1)&&w.ledger->publish(t5,55),"槽位重用：舊 token 被拒，新 token 有效",out);
        World o;const auto ro=o.add_route("hostA");o.add_env("A",ro);Token ft;o.start("A",true,ft);const int used_before=int(w.used());
        check(!w.ledger->publish(ft,1)&&int(w.used())==used_before,"外來 World 的 token 被拒，不影響本 World",out);
        w.drain();w.normal_round(5);check(w.env("A")->value==55&&w.c.double_apply==0,"一般輪套用新結果，沒有重複套用",out);I(w,out);dump(w,out);
        out<<"  註：結果套用不會提高資料來源版本；版本只由 external_change（fixture）改變。\n";
    }
    {   out<<"\n== R6 真實 std::thread 生產者（只用 token＋帳本）；擁有者結算；非擁有者變更被拒 ==\n";
        World w;const auto ra=w.add_route("hostA");w.add_env("A",ra);std::vector<Token> t(4);for(auto& x:t)w.start("A",true,x);w.lose_route(ra);
        auto ledger=w.ledger;std::atomic<int> go{0},done{0};std::atomic<int> dup_ok{0};
        std::thread worker([ledger,t,&go,&done,&dup_ok]{for(int k=1;k<=4;++k){while(go.load()<k)std::this_thread::yield();
            ledger->publish(t[k-1],k*100);if(ledger->publish(t[k-1],1))++dup_ok;done.store(k);}});
        auto wait=[&](int k){auto dl=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(done.load()<k&&std::chrono::steady_clock::now()<dl)std::this_thread::yield();return done.load()>=k;};
        go=1;bool ok=wait(1);go=2;ok&=wait(2);w.drain();
        const auto ra2=w.add_route("hostA");const auto a=w.begin_resume("A",ra2);
        go=3;ok&=wait(3);w.drain();w.resume_step(*a,1);out<<trace(w,"A");go=4;ok&=wait(4);w.drain();w.resume_step(*a,1);out<<trace(w,"A");
        worker.join();
        check(ok&&dup_ok==0,"worker 依擁有者控制的順序完成（期限只防卡住）；重複發布全被拒",out);
        check(w.attempt_of("A")->batch.size()==2&&w.commit_resume(*a)==R::Ok&&w.retained(0)==2,"本批固定 2；worker 後來的 2 份留到一般輪",out);
        w.normal_round(2);check(w.c.applied==4&&w.used()==0,"一般輪處理後全部處置",out);I(w,out);
        World v;const auto rv=v.add_route("hostA");v.add_env("A",rv);Token tv;v.start("A",true,tv);v.lose_route(rv);v.ledger->publish(tv,1);v.drain();const auto rv2=v.add_route("hostA");
        const auto before=full(v);Token tt;
        std::thread off([&]{v.submit("A");v.start("A",true,tt);v.lose_route(rv2);v.add_route("x");v.begin_resume("A",rv2);v.resume_step(1,1);v.commit_resume(1);
            v.drain();v.normal_round(5);v.external_change("A");v.retarget("A");v.fault("A");v.safe_cleanup("A");v.run_queued("A",tt);});
        off.join();
        std::thread off2([&]{(void)v.take_receipts();});off2.join();
        check(full(v)==before,"非擁有者執行緒的所有變更入口（含取回回條）：狀態、計數、日誌都不變",out);dump(w,out);v.log.clear();
    }
    // ---- review additions ----
    {   out<<"\n== R7 A 恢復期間，獨立的 B 持續補滿被釋放的槽位 ==\n";
        World w;const auto ra=w.add_route("hostA"),rb=w.add_route("hostB");w.add_env("A",ra);w.add_env("B",rb);
        Token ta[3],tb[3];for(auto& x:ta)w.start("A",true,x);for(auto& x:tb)w.start("B",false,x);w.lose_route(ra);
        for(int i=0;i<3;++i)w.ledger->publish(ta[i],i+1);w.drain();
        const auto ra2=w.add_route("hostA");const auto a=w.begin_resume("A",ra2);int bdone=0,bstarted=3;bool refilled=true;
        for(int s=0;s<3;++s){w.resume_step(*a,1);w.ledger->publish(tb[s],50+s);w.drain();w.normal_round(1);++bdone;
            Token n;refilled&=w.start("B",false,n)==R::Ok;++bstarted;if(s<2)tb[s]=n;out<<trace(w,"A");I(w,out);
            check(w.attempt_of("A")->batch.size()==3,"B 的新結果不進入 A 的固定本批",out);}
        Token last;const bool pre=w.start("B",false,last)==R::Ok;out<<"  提交前 B 補位="<<(pre?"成功":"容量滿被拒")<<" 使用槽位="<<w.used()<<"\n";
        check(refilled&&w.commit_resume(*a)==R::Ok&&w.env("A")->applied==3,"B 補滿槽位不阻擋 A 提交；A 三份各套用一次",out);
        check(w.env("B")->applied==bdone&&w.env("B")->state==EnvState::Running,"B 一直執行並處理自己的結果",out);I(w,out);w.log.clear();
    }
    {   out<<"\n== R8 途徑失效／安裝／恢復循環遠超途徑容量（fixture 上限 4）==\n";
        World w;const auto ra=w.add_route("hostA"),rb=w.add_route("hostB");w.add_env("A",ra);w.add_env("B",rb);
        std::uint64_t cur=ra;std::vector<std::uint64_t> old;std::vector<std::uint64_t> old_att;bool all_ok=true;
        for(int i=0;i<20;++i){w.lose_route(cur);old.push_back(cur);const auto nr=w.add_route("hostA");all_ok&=nr!=0;
            const auto a=w.begin_resume("A",nr);all_ok&=a&&w.commit_resume(*a)==R::Ok;if(a)old_att.push_back(*a);cur=nr;}
        out<<"  循環 20 次後途徑表大小="<<w.routes.size()<<"\n";
        check(all_ok,"20 次循環都成功（不以總次數限制壽命）",out);
        check(w.routes.size()<=4,"途徑表大小有上限（死掉的項目被回收）",out);
        bool olds_invalid=true;for(auto r:old)olds_invalid&=!w.begin_resume("A",r)&&(w.route(r)==nullptr||!w.route(r)->alive);
        bool atts_invalid=true;for(auto x:old_att)atts_invalid&=w.resume_step(x,1)==R::RefusedStale&&w.commit_resume(x)==R::RefusedStale;
        check(olds_invalid&&atts_invalid,"所有舊途徑與舊嘗試編號（含同名替換）都無效",out);
        Token b;check(w.route(rb)&&w.route(rb)->alive&&w.start("B",false,b)==R::Ok,"B 的途徑不受影響",out);I(w,out);w.log.clear();
    }
    {   out<<"\n== R9 逐筆回條：未執行、已取消、已完成（與 UI 處置分開）==\n";
        World w;const auto ra=w.add_route("hostA"),rb=w.add_route("hostB");w.add_env("A",ra);w.add_env("B",rb);
        Token tc,tk;w.start("A",false,tc);w.start("A",true,tk);w.submit("A");const auto id1=w.last_action;w.submit("A");const auto id2=w.last_action;
        check(w.take_receipts().empty(),"尚無終態：沒有回條",out);
        w.lose_route(ra);I(w,out);auto r1=w.take_receipts();
        bool ok1=r1.size()==2&&id1!=id2;for(auto& r:r1)ok1&=r.kind==Receipt::NotExecuted&&!r.ui_pending&&r.env==0;
        ok1&=r1.size()==2&&r1[0].id==id1&&r1[1].id==id2;
        check(ok1&&w.env("A")->value==0,"兩個已受理未開始的動作各得一張「未執行」回條，編號正確，無 UI 效果",out);
        w.lose_route(ra);const auto ra2=w.add_route("hostA");const auto a=w.begin_resume("A",ra2);w.lose_route(ra2);
        check(w.take_receipts().empty()&&w.env("A")->not_executed==2,"重複失效、恢復途中再失效：不重複發回條",out);
        w.ledger->publish(tc,0,true);w.ledger->publish(tk,9);w.drain();I(w,out);auto r2=w.take_receipts();
        bool ok2=r2.size()==2;for(auto& r:r2){if(r.id==tc.result)ok2&=r.kind==Receipt::Cancelled&&!r.ui_pending;else ok2&=r.id==tk.result&&r.kind==Receipt::Completed&&r.ui_pending;}
        check(ok2&&w.retained(0)==1&&w.env("A")->value==0,"已取消回條（沒有 UI 內容）與已完成回條（UI 仍待處置）各一張；暫停中不套用",out);
        check(w.take_receipts().empty(),"回條只交付一次",out);
        const auto ra3=w.add_route("hostA");const auto b=w.begin_resume("A",ra3);w.resume_step(*b,5);w.commit_resume(*b);
        check(w.take_receipts().empty()&&w.env("A")->applied==1&&w.env("A")->value==9,"恢復時套用結果不產生第二張命令回條（終態與 UI 處置分開）",out);
        Token tn;w.start("A",true,tn);w.ledger->publish(tn,4);w.drain();auto r3=w.take_receipts();
        check(r3.size()==1&&r3[0].id==tn.result&&r3[0].kind==Receipt::Completed&&r3[0].ui_pending&&w.normal_round(1)==1&&w.env("A")->value==4,"執行中的完成：先有回條，一般輪再套用",out);I(w,out);
        (void)a;World p;const auto pa=p.add_route("hostA");p.add_env("A",pa);p.add_env("B",p.add_route("hostB"));Token q[6];for(auto& x:q)p.start("A",true,x);
        for(int i=0;i<3;++i){p.submit("A");p.submit("B");}   // 6 commands + 6 queued actions = receipt_cap (12)
        for(int i=0;i<6;++i)p.ledger->publish(q[i],i);p.drain();p.normal_round(6);Token z;
        check(p.used()==0&&p.start("A",true,z)==R::RefusedNoCapacity,"回條未取回：結果槽位已空，但回條額度滿 → 新工作在受理前被拒",out);
        check(p.take_receipts().size()==6&&p.start("A",true,z)==R::Ok,"取回回條後才能再受理",out);I(p,out);w.log.clear();p.log.clear();
    }
    out<<"\n不變式檢查次數="<<invs<<"\nresume scenarios v2 "<<(fails?"FAIL":"PASS")<<" failures="<<fails<<'\n';
}

int resume_scenarios_main(){const char* log_name="resume-scenarios-v2.log";std::ofstream f(log_name,std::ios::binary);
    try{scenarios(f);}catch(const std::exception& e){f<<"FAIL exception "<<e.what()<<'\n';++fails;}
    printf("resume scenarios failures=%d invariant_checks=%d -> %s\n",fails,invs,log_name);return fails?1:0;}

// ---------- interactive companion (simulated; buttons call the model one transition at a time) ----------
static std::unique_ptr<World> W;static HWND st,lb;static std::uint64_t routeA=0,routeB=0,att=0;static std::vector<Token> toks;static std::string guide;
struct Step{const char* text;std::function<void(World&)> act;};static std::vector<Step> steps;static size_t si=0;
static std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring r(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n);return r;}
static void lose(World& w){w.L(std::string("失去 A 途徑：")+name(w.lose_route(routeA)));}
static void install(World& w){routeA=w.add_route("hostA");}
static void begin(World& w){auto a=w.begin_resume("A",routeA);if(a)att=*a;}
static void step1(World& w){w.L(std::string("處理一份：")+name(w.resume_step(att,1)));}
static void commit(World& w){w.L(std::string("提交恢復：")+name(w.commit_resume(att)));}
static void complete1(World& w){for(auto& t:toks)if(w.ledger->publish(t,int(t.result%100))){w.L("一個已開始的 A 工作完成並發布");break;}w.drain();}
static void round1(World& w){w.L("一般處理輪：處理 "+std::to_string(w.normal_round(1))+" 份");}
static void change(World& w){w.external_change("A");}
static void take(World& w){auto r=w.take_receipts();std::string s="取回回條 "+std::to_string(r.size())+" 張：";
    for(auto& x:r)s+=std::string(name(x.kind))+"#"+std::to_string(x.id)+(x.ui_pending?"(結算時 UI 待處置) ":" ");w.L(s);}   // ui_pending is a settlement-time snapshot, not live state
static void reset(int k){W=std::make_unique<World>();auto& w=*W;att=0;toks.assign(5,{});si=0;
    routeA=w.add_route("hostA");routeB=w.add_route("hostB");w.add_env("A",routeA);w.add_env("B",routeB);
    for(auto& t:toks)w.start("A",true,t);Token b;w.start("B",false,b);w.submit("A");
    const std::vector<Step> pre={{"失去 A 途徑（A 暫停；未開始動作回報未執行）",lose},{"完成一個 A 工作（留存）",complete1},{"再完成一個（留存）",complete1},
        {"安裝新 A 途徑（不會自動恢復）",install},{"請求恢復（固定本批）",begin}};
    steps=pre;
    if(k==2){guide="引導2：恢復期間持續到達";
        steps.insert(steps.end(),{{"新結果到達（不加入本批）",complete1},{"處理一份",step1},{"新結果到達",complete1},{"處理一份（本批完成）",step1},
            {"提交前又到一份",complete1},{"提交恢復",commit},{"一般處理輪",round1},{"一般處理輪",round1},{"一般處理輪",round1}});}
    else if(k==3){guide="引導3：重試不重複套用";
        steps.insert(steps.end(),{{"處理一份（已套用）",step1},{"失去 A 途徑（嘗試失效）",lose},{"舊嘗試處理一份（被拒）",step1},{"安裝新途徑",install},
            {"再次請求恢復（新快照只含剩下的）",begin},{"處理一份",step1},{"提交恢復",commit}});}
    else{guide="引導1：恢復途中途徑失效";
        steps.insert(steps.end(),{{"處理一份",step1},{"失去 A 途徑",lose},{"提交舊嘗試（被拒）",commit},{"安裝新途徑",install},{"請求恢復",begin},{"處理一份",step1},{"提交恢復",commit}});}
}
static void refresh(){auto& w=*W;std::ostringstream o;
    o<<"暫停／恢復原型 v2（模擬）  "<<guide<<"\r\n下一步："<<(si<steps.size()?std::to_string(si+1)+"/"+std::to_string(steps.size())+" "+steps[si].text:std::string("（引導結束，可自由操作）"))<<"\r\n";
    for(auto& e:w.envs){Route* r=w.route(e.route);Attempt* a=w.attempt_of(e.name);
        o<<e.name<<"："<<name(e.state)<<"  途徑 #"<<e.route<<" 版本"<<(r?r->version:0)<<(r&&r->alive?" 有效":" 失效")<<"  值="<<e.value<<"  資料版本="<<e.data_version
         <<"  未執行="<<e.not_executed<<" 取消請求="<<e.cancel_requests<<"\r\n   "
         <<(e.attempt?"進行中的恢復嘗試 #"+std::to_string(e.attempt)+"  固定本批="+std::to_string(a->batch.size())+" 已處理="+std::to_string(a->cursor)
                       +"  等待恢復提交="+(a->cursor==a->batch.size()?"是（提交時仍會再檢查）":"否")
                     :"無進行中的恢復嘗試"+(a->id?"（上次嘗試 #"+std::to_string(a->id)+"：本批 "+std::to_string(a->batch.size())+" 份，處理到 "+std::to_string(a->cursor)+"）":std::string()))
         <<"  留存結果="<<w.retained(w.idx(&e))
         <<"  進行中="<<w.count(w.idx(&e),Slot::Running)<<"  UI 處置：已套用 "<<e.applied<<" 不適用 "<<e.not_applicable<<"\r\n";}
    o<<"途徑表 "<<w.routes.size()<<"/"<<World::route_cap<<"  未取回回條 "<<w.outbox.size()<<"  回條額度 "<<w.receipt_credits<<"/"<<World::receipt_cap<<"\r\n";
    o<<"終態結算 "<<w.c.settled<<"（其中無結果 "<<w.c.no_result<<"）  容量 使用 "<<w.used()<<"/"<<World::slot_cap<<" 預留 "<<w.c.reserved<<" 歸還 "<<w.c.freed;
    SetWindowTextW(st,wide(o.str()).c_str());SendMessageW(lb,LB_RESETCONTENT,0,0);for(auto& l:w.log)SendMessageW(lb,LB_ADDSTRING,0,(LPARAM)wide(l).c_str());
    SendMessageW(lb,LB_SETTOPINDEX,w.log.empty()?0:w.log.size()-1,0);}
static LRESULT CALLBACK rproc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_COMMAND&&W){auto& w=*W;const int id=LOWORD(wp);
        if(id>=1&&id<=3)reset(id);else if(id==20){if(si<steps.size()){w.L("—— 步驟 "+std::to_string(si+1)+"："+steps[si].text);steps[si].act(w);++si;}}
        else switch(id){case 30:lose(w);break;case 31:install(w);break;case 32:begin(w);break;case 33:step1(w);break;case 34:commit(w);break;
            case 35:complete1(w);break;case 36:round1(w);break;case 37:change(w);break;case 38:take(w);break;}
        refresh();return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,wp,lp);}
int resume_interactive(){
    WNDCLASSW wc{};wc.lpfnWndProc=rproc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"PauseResumePrototypeV1";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    HWND win=CreateWindowExW(0,wc.lpszClassName,L"暫停／恢復原型 v2（可丟棄、模擬）",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1180,760,nullptr,nullptr,wc.hInstance,nullptr);
    const wchar_t* labels[]={L"引導1 途中失效",L"引導2 持續到達",L"引導3 重試",L"▶ 下一步",L"失去A途徑",L"安裝新A途徑",L"請求恢復",L"處理一份",L"提交恢復",L"完成一個A工作",L"一般處理輪",L"A外部資料變更",L"取回回條"};
    int ids[]={1,2,3,20,30,31,32,33,34,35,36,37,38};
    for(int i=0;i<13;++i){int r=i<7?0:1,c=i<7?i:i-7;CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,10+c*150,10+r*36,144,30,win,(HMENU)(INT_PTR)ids[i],wc.hInstance,nullptr);}
    st=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,10,86,1140,150,win,nullptr,wc.hInstance,nullptr);
    lb=CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_BORDER|LBS_NOINTEGRALHEIGHT,10,242,1140,460,win,nullptr,wc.hInstance,nullptr);
    reset(1);refresh();MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}W.reset();return 0;}
