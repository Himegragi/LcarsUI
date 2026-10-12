#include "../../report_model.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <deque>
#include <functional>
#include <barrier>
#include <malloc.h>
using namespace rh;
static thread_local size_t allocs=0; static thread_local bool failNew=false;
#if !__has_feature(address_sanitizer)
void* operator new(size_t n){++allocs;if(failNew){failNew=false;throw std::bad_alloc();}if(auto p=malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{free(p);} void operator delete[](void*p)noexcept{free(p);}
void operator delete(void*p,size_t)noexcept{free(p);} void operator delete[](void*p,size_t)noexcept{free(p);}
void* operator new(size_t n,std::align_val_t a){++allocs;if(auto p=_aligned_malloc(n?n:1,(size_t)a))return p;throw std::bad_alloc();}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t)noexcept{_aligned_free(p);}void operator delete[](void*p,std::align_val_t)noexcept{_aligned_free(p);}
#endif
int fails=0;void ck(bool b,const char*s){printf("%s %s\n",b?"PASS":"FAIL",s);if(!b)++fails;}
void drain(World&w){while(w.settle()){} }
int env(World&w,Contract c=Contract::Handover){w.set_lost_receiver([](const LostNotice&){});return *w.add_env("A",c);}
void findings(){
 {World w(8);int a=env(w);auto h=w.fault(a);auto d=*w.deliver(h);d.callback.report();d.work.report();d.lease.report();drain(w);auto d2=w.deliver(h);
 printf("F1 redeliver=%d live=%zu invariant_errors=%zu\n",bool(d2),w.ledger().live(),w.invariants().size());
 auto old=d.ack.report();drain(w);printf("F1 old_ack=%d acked=%d new_ack_tracked=%d\n",int(old),w.handoffs.count(h)?w.handoffs.at(h).acked:true,w.handoffs.count(h)?bool(w.handoffs.at(h).ack):false);
 if(d2) { d2->callback.report();d2->work.report();d2->lease.report();d2->ack.report();try{drain(w);puts("F1 no exception");}catch(const std::exception&e){printf("F1 exception=%s\n",e.what());}}}
 {World w(4);int a=env(w);auto h=*w.start_command(a);h.report({123,true});drain(w);printf("F4 cancelled_env_value=%d\n",w.envs[a].value);}
 {World w(4);int a=env(w);auto h=*w.start_command(a);w.set_cleanup_safe(a,false);w.close(a);h.report({123});drain(w);w.collect(a);printf("F5 unsafe_collect_receipts=%zu outcomes=%zu ended=%d\n",w.receipts.size(),w.outcomes.size(),w.envs[a].ended);}
 {World w(4);int a=env(w);auto h=*w.start_command(a);h.report();auto r=h.report();printf("F6 duplicate_reason=%d empty_diagnostic=%zu\n",int(r),w.ledger().rejected(Reject::Empty));}
#if !__has_feature(address_sanitizer)
 {World w(4);int a=env(w);failNew=true;try{auto h=w.start_command(a);}catch(const std::bad_alloc&){}printf("F7 failed_start live=%zu commands=%zu receipts=%zu inv=%zu\n",w.ledger().live(),w.commands.size(),w.receipts.size(),w.invariants().size());}
#endif
}
void exceptionDrain(){
#if !__has_feature(address_sanitizer)
 World w(8);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);d.callback={};d.work={};bool caught=false;failNew=true;try{w.settle();}catch(const std::bad_alloc&){caught=true;}failNew=false;
 printf("F8 failed_settle caught=%d live=%zu cb_lost=%d work_lost=%d inv=%zu next_settle=%zu\n",caught,w.ledger().live(),w.handoffs.at(id).cb.lost,w.handoffs.at(id).work.lost,w.invariants().size(),w.settle());
#endif
}
void basic(){
 ck(!std::is_copy_constructible_v<ReportHandle>&&!std::is_copy_assignable_v<ReportHandle>&&!std::is_constructible_v<ReportHandle,std::weak_ptr<Ledger>,Ledger::Ref>,"handle copy/forge types rejected");
 {World w(4);int a=env(w);auto h=*w.start_command(a);h=std::move(h);ck(h.valid()&&h.report({5})==Reject::None,"self move preserves report");ck(h.report()==Reject::Empty,"second report empty");auto x=*w.start_command(a);auto y=*w.start_command(a);x=std::move(y);ck(!y.valid()&&x.report()==Reject::None,"move onto live handle transfers");drain(w);ck(w.providerLost==1&&w.ledger().live()==0&&w.invariants().empty(),"overwritten live handle dropped exactly once");}
 {World w(8);int a=env(w,Contract::ExplicitAck);{auto h=*w.start_command(a);}drain(w);ck(w.providerLost==1,"result drop provider lost");auto ks=w.collect(a);ks.clear();drain(w);ck(w.receipts.size()==1&&!w.receipts.begin()->second.handedOver&&w.ledger().live()==0,"result ack drop retries delivery");ks=w.collect(a);ks[0].report();drain(w);ck(w.outcomes.size()==1,"result ack retry settles once");}
 {World w(8);int a=env(w);auto id=w.fault(a);{auto ds=*w.deliver(id);}drain(w);auto&h=w.handoffs.at(id);ck(h.cb.lost&&h.work.lost&&h.lease.lost&&h.cb.active&&h.work.active&&h.lease.active&&w.lost_parts().size()==3&&!h.acked&&!h.ack&&w.ledger().live()==0,"all delivery kinds drop; ack not accepted; three lost obligations");w.set_cleanup_safe(a,false);ck(w.declare_ended(id,Kind::CallbackEnd)==Declare::Done,"per-item declaration independent of cleanup flag");w.set_cleanup_safe(a,true);ck(w.declare_ended(id,Kind::WorkEnd)==Declare::Done && w.declare_ended(id,Kind::LeaseEnd)==Declare::Done,"remaining lost parts declared separately");auto d=*w.deliver(id);d.callback.report();d.work.report();d.lease.report();d.ack.report();drain(w);ck(w.handoffs.empty(),"handoff ack retry completes");}
 {World w(8);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);w.reassign(id);auto before=w.ledger().live();d.ack={};ck(w.ledger().live()==before,"revoked handoff ack drop inert");auto ds=*w.start_command(a);ds.report();drain(w);w.abandon(a);}
 {World w(4);int a=env(w,Contract::ExplicitAck);auto h=*w.start_command(a);h.report();drain(w);auto ks=w.collect(a);w.abandon(a);ks.clear();ck(w.ledger().live()==0&&w.outcomes.size()==1&&w.invariants().empty(),"revoked result ack drop inert");}
 {World w(8);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);w.reassign(id);auto live=w.ledger().live();ck(d.ack.report()==Reject::Revoked&&!d.ack.valid()&&w.ledger().live()==live&&!w.handoffs.at(id).acked&&w.ledger().rejected(Reject::Revoked)==1,"Revoked preserves obligation state except diagnostic and consumed handle");ck(d.ack.report()==Reject::Empty,"rejected handle then empty");}
 {ReportHandle h;{World w(1);int a=env(w);h=*w.start_command(a);}ck(h.report()==Reject::WorldGone&&h.report()==Reject::Empty,"WorldGone and subsequent Empty");}
 {World w(1);int a=env(w,Contract::ExplicitAck);auto h=*w.start_command(a);h.report();drain(w);auto blocker=*w.start_command(a);ck(w.collect(a).empty()&&!w.receipts.begin()->second.handedOver,"ack capacity failure keeps receipt undelivered");w.close(a);ck(!w.envs[a].ended,"close retains pending obligations");w.set_cleanup_safe(a,false);ck(!w.abandon(a),"unsafe abandon refused");w.set_cleanup_safe(a,true);ck(*w.abandon(a)==1&&!w.envs[a].ended,"explicit abandon only terminal result");blocker.report();drain(w);auto ks=w.collect(a);ks[0].report();drain(w);ck(w.envs[a].ended&&w.outcomes.size()==2,"late result accepted and finalizes once");}
}
void stress(){
 constexpr int N=4000,T=12;World w(48);int a=env(w);std::mutex m;std::deque<std::pair<int,ReportHandle>> q;std::atomic<bool> stop=false;std::atomic<int> accepted=0,lost=0,duplicates=0;std::atomic<size_t> allocations=0;std::vector<std::thread> ts;
 for(int j=0;j<T;++j)ts.emplace_back([&]{for(;;){std::pair<int,ReportHandle> job;bool got=false;{std::lock_guard g(m);if(!q.empty()){job=std::move(q.front());q.pop_front();got=true;}}if(!got){if(stop)return;std::this_thread::yield();continue;}auto before=allocs;if(job.first%3){if(job.second.report({job.first})==Reject::None)++accepted;if(job.second.report()==Reject::Empty)++duplicates;}else{job.second={};++lost;}allocations+=allocs-before;}});
 size_t bad=0,maxDrain=0;for(int i=0;i<N;){if(auto h=w.start_command(a)){std::lock_guard g(m);q.emplace_back(i++,std::move(*h));}auto n=w.settle();if(n>maxDrain)maxDrain=n;if(!w.invariants().empty())++bad;}
 stop=true;for(auto&t:ts)t.join();drain(w);w.collect(a);ck(bad==0&&w.invariants().empty()&&w.ledger().live()==0&&w.outcomes.size()==N&&accepted+lost==N&&duplicates==accepted,"concurrent reports/drop/settle/new obligations accounting");printf("STRESS issued=%d accepted=%d lost=%d bad=%zu max_drain=%zu allocations=%zu\n",N,int(accepted),int(lost),bad,maxDrain,size_t(allocations));
#if !__has_feature(address_sanitizer)
 ck(allocations==0,"Release concurrent report/drop new/new[]/aligned allocation count zero");
#endif
 // Each world contains all six handle kinds. Worker ownership never aliases the same handle.
 std::atomic<size_t> lifetimeAllocs=0;int acceptedDeath=0,gone=0;for(int round=0;round<300;++round){auto w=std::make_unique<World>(16);int a=env(*w,Contract::ExplicitAck);auto pre=*w->start_command(a);pre.report();drain(*w);auto ks=w->collect(a);auto id=w->fault(a);auto d=*w->deliver(id);std::vector<ReportHandle> hs;hs.push_back(*w->start_command(a));hs.push_back(std::move(d.callback));hs.push_back(std::move(d.work));hs.push_back(std::move(d.lease));hs.push_back(std::move(d.ack));hs.push_back(std::move(ks[0]));std::barrier gate(7);std::atomic<int> ok=0,dead=0;std::vector<std::thread> workers;
 for(int j=0;j<6;++j)workers.emplace_back([&,j,h=std::move(hs[j])]()mutable{gate.arrive_and_wait();auto before=allocs;if((round+j)%2){auto r=h.report();if(r==Reject::None)++ok;else if(r==Reject::WorldGone)++dead;else std::abort();}else h={};lifetimeAllocs+=allocs-before;});
 if(round%3==0){w.reset();gate.arrive_and_wait();}else{gate.arrive_and_wait();if(round%3==1)w->settle();w.reset();}for(auto&t:workers)t.join();acceptedDeath+=ok;gone+=dead;}
 printf("LIFETIME path_allocations=%zu\n",size_t(lifetimeAllocs));printf("LIFETIME rounds=300 handles=1800 report_accepted=%d world_gone=%d drops=900\n",acceptedDeath,gone);ck(acceptedDeath+gone==900,"all six kinds concurrent settle/destruction safe in finite run");
}
int main(){basic();findings();exceptionDrain();stress();printf("probe_unexpected_failures=%d\n",fails);return fails?1:0;}



