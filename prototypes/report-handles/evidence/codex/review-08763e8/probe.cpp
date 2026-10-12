#include "../../report_model.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <malloc.h>
using namespace rh;
static thread_local long failAt=-1;
static thread_local size_t allocations=0;
#if !__has_feature(address_sanitizer)
void* operator new(size_t n){++allocations;if(failAt==0){failAt=-1;throw std::bad_alloc();}if(failAt>0)--failAt;if(auto p=malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{free(p);}void operator delete[](void*p)noexcept{free(p);}
void operator delete(void*p,size_t)noexcept{free(p);}void operator delete[](void*p,size_t)noexcept{free(p);}
void* operator new(size_t n,std::align_val_t a){++allocations;if(failAt==0){failAt=-1;throw std::bad_alloc();}if(failAt>0)--failAt;if(auto p=_aligned_malloc(n?n:1,(size_t)a))return p;throw std::bad_alloc();}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t)noexcept{_aligned_free(p);}void operator delete[](void*p,std::align_val_t)noexcept{_aligned_free(p);}
#endif
int failures=0,checks=0;void ck(bool b,const char*label){++checks;if(!b){++failures;printf("FAIL %s\n",label);}}
void drain(World&w){while(w.settle()){};ck(w.invariants().empty(),"boundary invariants");}
int env(World&w){return *w.add_env("A",Contract::ExplicitAck);}
ReportHandle& handle(DeliveryHandles&d,int i){return i==0?d.callback:i==1?d.work:d.lease;}
constexpr Kind kinds[]={Kind::CallbackEnd,Kind::WorkEnd,Kind::LeaseEnd};
void declarations(){
 for(int i=0;i<3;++i){World w(16);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);
  ck(w.declare_ended(id,kinds[i])==Declare::NotLost,"held refused");
  handle(d,i).report();ck(w.declare_ended(id,kinds[i])==Declare::NotLost,"filled before settle refused");drain(w);
  ck(w.declare_ended(id,kinds[i])==Declare::NotLost&&w.declarations.empty(),"normal reported refused");
 }
 for(int mask=0;mask<8;++mask){std::array<int,4> order{0,1,2,3};do{World w(16);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);size_t expected=0;
  for(int i=0;i<3;++i)if(mask&(1<<i)){handle(d,i)={};++expected;ck(w.declare_ended(id,kinds[i])==Declare::NotLost,"drop undetected refused");}drain(w);
  for(int j=0;j<4;++j){int i=order[j];if(i==3){d.ack.report();drain(w);}else if(mask&(1<<i))ck(w.declare_ended(id,kinds[i])==Declare::Done,"lost declaration accepted");else{handle(d,i).report();drain(w);}
   ck(w.handoffs.count(id)==size_t(j!=3),"release requires every part and ack");ck(w.invariants().empty(),"per-step invariants");
   if(i<3&&(mask&(1<<i)))ck(w.declare_ended(id,kinds[i])==(j==3?Declare::Unknown:Declare::NotLost),"repeat refused");
  }
  ck(w.declarations.size()==expected&&w.reportedEnds==3-expected&&w.ledger().live()==0,"separate records and capacity");
 }while(std::next_permutation(order.begin(),order.end()));}
 {World w(8);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);d.callback={};drain(w);
  for(auto k:{Kind::Result,Kind::HandoffAck,Kind::ResultAck,static_cast<Kind>(255)})ck(w.declare_ended(id,k)==Declare::Unknown,"invalid kind");
  ck(w.declare_ended(999999,Kind::CallbackEnd)==Declare::Unknown&&w.declarations.empty(),"unknown id");}
 for(int lost=0;lost<2;++lost){World w(16);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);handle(d,lost)={};drain(w);ck(w.reassign(id),"reassign accepted");ck(d.ack.report()==Reject::Revoked,"old ack revoked");
  ck(!w.deliver(id),"lost and held block redelivery");ck(w.declare_ended(id,kinds[lost])==Declare::Done,"declare lost");ck(!w.deliver(id),"other held callback/work blocks redelivery");handle(d,1-lost).report();ck(!w.deliver(id),"other filled still blocks until settle");drain(w);
  auto d2=w.deliver(id);ck(bool(d2)&&!d2->lease.valid(),"redelivery after old callback/work done");d2->callback.report();d2->work.report();d2->ack.report();drain(w);ck(w.handoffs.count(id)==1,"old lease keeps handoff");d.lease.report();drain(w);ck(w.handoffs.empty(),"lease ends release");}
 printf("DECLARATIONS 192 completion permutations plus refusal and reassign cases\n");
}
void reentry(){
 const char*names[]={"settle","deliver","reassign","collect","abandon","start_command","close","set_cleanup_safe","set_lost_receiver","add_env","fault"};
 for(int mode=0;mode<2;++mode)for(int method=0;method<11;++method){World w(32);int a=env(w);auto before=*w.start_command(a);auto id=w.fault(a);auto d=*w.deliver(id);auto after=*w.start_command(a);size_t calls=0,refused=0;bool owner=true,readok=true;auto tid=std::this_thread::get_id();
  auto mutate=[&]{switch(method){case 0:w.settle();break;case 1:w.deliver(id);break;case 2:w.reassign(id);break;case 3:w.collect(a);break;case 4:w.abandon(a);break;case 5:w.start_command(a);break;case 6:w.close(a);break;case 7:w.set_cleanup_safe(a,false);break;case 8:w.set_lost_receiver({});break;case 9:w.add_env("B",Contract::Handover);break;case 10:w.fault(a);break;}};
  w.set_lost_receiver([&](const LostNotice&n){++calls;owner &= std::this_thread::get_id()==tid;readok &= !w.lost_parts().empty()&&w.ledger().capacity()==32&&n.handoff==id;(void)w.invariants();ck(w.declare_ended(id,n.kind)==Declare::Reentrant,"declare reentry");if(mode==0){try{mutate();}catch(const std::logic_error&){++refused;}}else mutate();});
  std::thread([cb=std::move(d.callback),wk=std::move(d.work),ls=std::move(d.lease)]()mutable{}).join();before.report({1});after.report({2});d.ack.report();drain(w);drain(w);
  ck(calls==3&&owner&&readok,"owner single notices read queries");ck(w.reentryRefused==6,"all reentry counted");ck((mode==0?refused==3&&w.receiverFailures==0&&w.lostNotified==3:w.receiverFailures==3&&w.lostNotified==0),"receiver exception caught outside or inside");
  ck(w.commands.size()==2&&!w.commands.begin()->second.running&&!w.commands.rbegin()->second.running&&w.envs[a].value==2&&!w.envs[a].closed&&w.envs[a].cleanupSafe&&w.envs.size()==1&&w.handoffs.size()==1&&w.handoffs.at(id).acked,"outer batch intact");
  for(auto k:kinds)ck(w.declare_ended(id,k)==Declare::Done,"guard restored after outer settle");ck(w.handoffs.empty()&&w.invariants().empty(),"cleanup after receiver");printf("REENTRY %s mode=%s calls=%zu refused=%zu failures=%zu\n",names[method],mode?"uncaught":"caught",calls,refused,w.receiverFailures);
 }
 {World w(16);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);int n=0;w.set_lost_receiver([&](const LostNotice&){++n;throw 7;});d.callback={};d.work={};d.lease={};drain(w);drain(w);ck(n==3&&w.receiverFailures==3&&w.lost_parts().size()==3,"arbitrary throwing receiver at most once");}
 {World w(16);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);d.callback={};drain(w);int n=0;w.set_lost_receiver([&](const LostNotice&){++n;});drain(w);d.work={};drain(w);ck(n==1&&w.lostUnnotified==1&&w.lostNotified==1,"registration no replay, future notices delivered");}
 {World w(8);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);int refused=0;std::thread t([&]{try{w.settle();}catch(const std::logic_error&){++refused;}try{w.declare_ended(id,Kind::CallbackEnd);}catch(const std::logic_error&){++refused;}try{w.set_lost_receiver({});}catch(const std::logic_error&){++refused;}});t.join();ck(refused==3&&w.ownerThreadViolations==3,"wrong owner refused");}
}
void allocationAndRetry(){
#if !__has_feature(address_sanitizer)
 for(int mode=0;mode<3;++mode){World w(32);int a=env(w);size_t count=0;std::array<DeliveryHandles,3> ds;for(auto&d:ds)d=*w.deliver(w.fault(a));if(mode)w.set_lost_receiver([&](const LostNotice&){++count;if(mode==2)throw 1;});for(auto&d:ds){d.callback={};d.work={};d.lease={};}auto before=allocations;failAt=0;auto n1=w.settle(),n2=w.settle();failAt=-1;auto used=allocations-before;ck(n1==8&&n2==1&&used==0,"9 lost items across batches no allocation");ck(mode?count==9:w.lostUnnotified==9,"nine notices");ck(w.invariants().empty(),"no allocation end invariants");printf("ALLOC mode=%d slots=9 batches=8+1 allocations=%zu\n",mode,used);}
 int triggered=0;
 for(int pos=0;pos<8;++pos){World w(32);int a=env(w);auto h1=w.fault(a);auto d1=*w.deliver(h1);auto result=*w.start_command(a);auto h2=w.fault(a);auto d2=*w.deliver(h2);result.report();drain(w);auto ack=w.collect(a);size_t c1=0,c2=0;w.set_lost_receiver([&](const LostNotice&n){if(n.handoff==h1)++c1;else if(n.handoff==h2)++c2;});d1.callback={};d2.callback={};ack[0].report();failAt=pos;bool threw=false;try{w.settle();}catch(const std::bad_alloc&){threw=true;++triggered;}failAt=-1;
  if(threw)ck(c1==1&&c2==0&&w.outcomes.empty()&&w.receipts.size()==1,"failure occurs after first notice before second");drain(w);drain(w);ck(c1==1&&c2==1&&w.lostNotified==2&&w.outcomes.size()==1&&w.receipts.empty(),"retry no duplicate or lost notices/outcome");}
 ck(triggered>0,"retry failure injection exercised");printf("RETRY allocation_failures=%d scans=8\n",triggered);
 {World w(8);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);d.callback={};drain(w);failAt=0;bool threw=false;try{w.declare_ended(id,Kind::CallbackEnd);}catch(const std::bad_alloc&){threw=true;}failAt=-1;ck(threw&&w.declarations.empty()&&w.handoffs.at(id).cb.active&&w.handoffs.at(id).cb.lost&&w.invariants().empty(),"declaration bad_alloc preserves state");ck(w.declare_ended(id,Kind::CallbackEnd)==Declare::Done,"declaration allocation retry");}
#else
 puts("SKIP allocator replacement and bad_alloc injection under ASan");
#endif
}
void observations(){
 World w(8);int a=env(w);auto id=w.fault(a);auto d=*w.deliver(id);size_t inside=0;w.set_lost_receiver([&](const LostNotice&){inside=w.invariants().size();});d.callback={};drain(w);printf("OBS receiver_invariant_errors=%zu after_settle=%zu\n",inside,w.invariants().size());
}
int main(){declarations();reentry();allocationAndRetry();observations();printf("expanded_checks=%d expanded_failures=%d\n",checks,failures);return failures?1:0;}
