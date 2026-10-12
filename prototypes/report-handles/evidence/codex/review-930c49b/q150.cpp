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

void rings(){
 for(size_t cap: {0,1,2,3,16}) {
  World w(16,cap); w.set_lost_receiver([](const LostNotice&){});
  int a=env(w); auto h=w.fault(a); auto d=*w.deliver(h);
  auto c=*w.start_command(a);ck(c.report()==Reject::None,"initial report accepted");
  std::vector<Reject> expected; size_t empty=0,revoked=0;
  for(int i=0;i<80;++i){
   Reject want;
   if(i%4==0){w.reassign(h);want=Reject::Revoked;ck(d.ack.report()==want,"revoked category");++revoked;
    // Callback/work normal completions permit a fresh delivery while retaining lease.
    d.callback.report();d.work.report();drain(w);d=*w.deliver(h);
   }else{want=Reject::Empty;ck(c.report()==want,"empty category");++empty;}
   expected.push_back(want);
   auto rec=w.ledger().recent_rejects();size_t n=std::min(cap,expected.size());
   ck(rec.size()==n,"ring size bounded");
   for(size_t j=0;j<n;++j){size_t idx=expected.size()-n+j;ck(rec[j].seq==idx&&rec[j].reason==expected[idx],"ring wrap oldest first exact reason");}
   ck(w.ledger().rejected(Reject::Empty)==empty&&w.ledger().rejected(Reject::Revoked)==revoked&&w.ledger().rejected(Reject::WorldGone)==0,"category counters exact");
  }
#if !__has_feature(address_sanitizer)
  auto before=allocations; failAt=0; for(int i=0;i<100;++i)c.report(); failAt=-1;ck(allocations==before,"record rejection zero allocations");
#endif
  printf("RING N=%zu events=80 wrap-and-reason checked\n",cap);
 }
 // Known pre-existing gap relative to unqualified Q147; observations, not suite expectations.
 {World w(4);int a=env(w);auto src=*w.start_command(a);auto dst=std::move(src);auto r=src.report();
 printf("Q147_GAP moved_from_return=%d Empty_count=%zu details=%zu world_alive=1\n",int(r),w.ledger().rejected(Reject::Empty),w.ledger().recent_rejects().size());
 ck(r==Reject::Empty&&w.ledger().rejected(Reject::Empty)==0,"known moved-from diagnostic gap reproduced");}
}
void receiverAndGate(){
 World w(32);int a=env(w);auto h=w.fault(a);auto live=w.ledger().live();
 for(int i=0;i<3;++i){ck(!w.deliver(h),"no receiver rejects all three obligations");ck(w.ledger().live()==live&&!w.handoffs.at(h).cb.active&&!w.handoffs.at(h).work.active&&!w.handoffs.at(h).lease.active,"no partial obligations");w.reassign(h);}
 ck(!w.set_lost_receiver({}),"empty initial receiver rejected");
 // Other entry points create Result/ResultAck or empty handoff, not callback/work/lease.
 auto c=*w.start_command(a);c.report();drain(w);auto ack=w.collect(a);ck(ack.size()==1,"result ack permitted without lost receiver");ack[0].report();drain(w);
 size_t old=0,now=0,third=0;
 ck(w.set_lost_receiver([&](const LostNotice&){++old;}),"first registration");
 auto d=*w.deliver(h);
 ck(!w.set_lost_receiver({}),"empty replacement keeps receiver");
 d.callback={};
 ck(w.set_lost_receiver([&](const LostNotice&){++now;}),"replace after drop before settle");
 drain(w);ck(old==0&&now==1,"notice routes to current receiver");
 ck(w.set_lost_receiver([&](const LostNotice&){++third;}),"replace after completed batch");
 d.work={};drain(w);ck(now==1&&third==1,"future notices use replacement");
 w.set_cleanup_safe(a,false);w.close(a);ck(!w.abandon(a),"unsafe abandon normal precondition");w.set_cleanup_safe(a,true);
 ck(w.invariants().empty(),"gate and replacement invariants");
}
void nested(){
 for(int throwMode=0;throwMode<2;++throwMode){
  World w(64);int a=env(w); std::array<uint64_t,5> ids{};std::array<DeliveryHandles,5> ds;
  std::array<std::array<int,3>,5> seen{};int depth=0,maxdepth=0;size_t replaceRefused=0,nestedCalls=0;
  w.set_lost_receiver([&](const LostNotice&n){
   ++depth;maxdepth=std::max(depth,maxdepth);
   size_t id=0;while(ids[id]!=n.handoff)++id;
   size_t k=n.kind==Kind::CallbackEnd?0:n.kind==Kind::WorkEnd?1:2;
   ++seen[id][k];ck(w.invariants().empty(),"nested receiver invariant consistent");
   ck(!w.set_lost_receiver([](const LostNotice&){}),"replace during nested delivery refused");++replaceRefused;
   if(depth==1&&nestedCalls++==0){ck(w.settle()==7,"nested consumes real remaining seven");ck(!w.set_lost_receiver([](const LostNotice&){}),"flag remains set after nested nonempty batch");++replaceRefused;}
   ck(w.settle()==0,"nested empty batch safe");
   ck(!w.set_lost_receiver([](const LostNotice&){}),"empty nested batch keeps flag");++replaceRefused;
   --depth;
   if(throwMode&&k==1)throw 37;
  });
  for(size_t i=0;i<ds.size();++i){ids[i]=w.fault(a);ds[i]=*w.deliver(ids[i]);ds[i].callback={};ds[i].work={};ds[i].lease={};}
  ck(w.settle()==8,"outer fixed batch stays eight");drain(w);drain(w);
  for(auto&arr:seen)for(int n:arr)ck(n==1,"nested every notice exactly once");
  ck(maxdepth==2&&w.lostNotified==size_t(throwMode?10:15)&&w.receiverFailures==size_t(throwMode?5:0),"nested exception accounting");
  ck(w.refusedReceiverChange==replaceRefused,"all nested flag guards counted");
  ck(w.set_lost_receiver([](const LostNotice&){}),"flag cleared after outer delivery");
  printf("NESTED notices=15 outer=8 nested=7 depth=%d throwing=%d\n",maxdepth,throwMode);
 }
}
void successfulMutations(){
 for(int method=0;method<9;++method){
  World w(32);int a=env(w);uint64_t trigger=0,target=0;int calls=0;
  std::optional<DeliveryHandles> delivered;std::vector<ReportHandle> collected;
  w.set_lost_receiver([&](const LostNotice&n){++calls;ck(w.invariants().empty(),"mutation receiver consistent");
   switch(method){
    case 0:ck(w.settle()==0,"receiver settle");break;
    case 1:delivered=w.deliver(target);ck(bool(delivered),"receiver deliver actually accepted");break;
    case 2:ck(w.reassign(target),"receiver reassign actually accepted");break;
    case 3:collected=w.collect(a);ck(collected.size()==1,"receiver collect actual result");break;
    case 4:ck(w.abandon(a)==std::optional<size_t>(1),"receiver abandon actual result");break;
    case 5:ck(w.declare_ended(n.handoff,n.kind)==Declare::Done,"receiver declare actual lost item");break;
    case 6:w.close(a);ck(w.envs[a].closed,"receiver close");break;
    case 7:w.set_cleanup_safe(a,false);ck(!w.envs[a].cleanupSafe,"receiver cleanup flag");break;
    case 8:ck(w.start_command(a).has_value(),"receiver start command");ck(w.add_env("B",Contract::Handover).has_value(),"receiver add env");w.fault(a);break;
   }
  });
  trigger=w.fault(a);auto dt=*w.deliver(trigger);target=w.fault(a);
  std::optional<DeliveryHandles> targetHandles;if(method==2)targetHandles=w.deliver(target);
  auto c=*w.start_command(a);c.report();drain(w);
  dt.callback={};drain(w);ck(calls==1&&w.receiverFailures==0,"receiver mutation delivered exactly once");
  ck(w.invariants().empty(),"mutation boundary invariant");
 }
 puts("MUTATIONS successful settle/deliver/reassign/collect/abandon/declare/close/cleanup/start/add/fault");
}
#if !__has_feature(address_sanitizer)
size_t mixed(int inject,int mode,int prefill,bool&caught){
 World w(64);int a=env(w);
 // Exercise outcome growth/no-growth and dropped Result paths (fixed strings use MSVC SSO).
 for(int i=0;i<prefill;++i){auto c=*w.start_command(a);c.report();drain(w);auto as=w.collect(a);as[0].report();drain(w);}
 auto h1=w.fault(a);std::array<int,6> seen{};size_t nested=0;
 w.set_lost_receiver([&](const LostNotice&n){
  int k=n.kind==Kind::CallbackEnd?0:n.kind==Kind::WorkEnd?1:2;
  if(n.handoff!=h1)k+=3;++seen[k];
  if(mode==2&&nested++==0){w.settle();ck(!w.set_lost_receiver([](const LostNotice&){}),"exception path nested flag kept");}
  if(mode==1&&k%2==0)throw 5;
 });
 auto d1=*w.deliver(h1);auto c=*w.start_command(a);c.report();drain(w);auto ack=w.collect(a);
 auto dropped=*w.start_command(a);auto h2=w.fault(a);auto d2=*w.deliver(h2);
 d1.callback={};d1.work={};d1.lease={};ack[0].report();dropped={};d2.callback={};d2.work={};d2.lease={};
 auto before=allocations;failAt=inject;
 try{ck(w.settle()==8,"mixed outer count");}catch(const std::bad_alloc&){caught=true;}
 failAt=-1;auto measured=allocations-before;
 ck(w.invariants().empty(),"batch exception state remains consistent");drain(w);drain(w);
 for(int n:seen)ck(n==1,"mixed retries each notice exactly once");
 ck(w.providerLost==1&&w.outcomes.size()==size_t(prefill+1),"mixed results applied once");
 ck(w.lostNotified+w.receiverFailures==6&&w.lostUnnotified==0,"mixed exact notification accounting");
 ck(w.set_lost_receiver([](const LostNotice&){}),"exception path flag cleared");
 return measured;
}
#endif
void exhaustiveAlloc(){
#if !__has_feature(address_sanitizer)
 int injected=0,total=0;
 for(int mode=0;mode<3;++mode)for(int fill:{0,7,8,15,16}){
  bool caught=false;auto n=mixed(-1,mode,fill,caught);ck(!caught,"baseline no exception");
  printf("ALLOC_BASE mode=%d outcomes=%d allocation_sites=%zu\n",mode,fill,n);
  for(size_t i=0;i<=n;++i){bool was=false;mixed(int(i),mode,fill,was);++total;
   ck(was==(i<n),"every measured allocation site individually fails");if(was)++injected;}
 }
 printf("ALLOC_SWEEP runs=%d actual_bad_alloc=%d\n",total,injected);
#else
 puts("SKIP exhaustive allocation under ASan");
#endif
}

void repeatedFailures(){
#if !__has_feature(address_sanitizer)
 World w(128);int a=env(w);std::array<uint64_t,3> ids{};std::array<DeliveryHandles,3> ds;
 std::vector<ReportHandle> acks;
 // Interleave groups of lost notices with acknowledgements that grow outcomes at 0, 8, 16.
 w.set_lost_receiver([](const LostNotice&){});
 for(int j=0;j<3;++j){
  ids[j]=w.fault(a);ds[j]=*w.deliver(ids[j]);
  for(int k=0;k<6;++k){auto c=*w.start_command(a);c.report();drain(w);auto x=w.collect(a);acks.push_back(std::move(x[0]));}
 }
 std::array<std::array<int,3>,3> seen{};int receiverCalls=0;
 w.set_lost_receiver([&](const LostNotice&n){++receiverCalls;int j=0;while(ids[j]!=n.handoff)++j;int k=n.kind==Kind::CallbackEnd?0:n.kind==Kind::WorkEnd?1:2;++seen[j][k];ck(w.invariants().empty(),"repeated failure receiver consistent");if(k==1)throw 17;});
 for(auto&d:ds){d.callback={};d.work={};d.lease={};}for(auto&ack:acks)ack.report();
 int injected=0;size_t rounds=0;
 while(w.ledger().live()>3&&rounds++<20){ // Three still-held handoff acks remain.
  for(int retry=0;retry<3;++retry){
   failAt=0;try{w.settle();}catch(const std::bad_alloc&){++injected;}failAt=-1;
   for(auto&arr:seen)for(int n:arr)ck(n<=1,"no duplicated prefix across repeated failures");
   ck(w.invariants().empty(),"each failed attempt preserves accounting");
  }
  w.settle();
 }
 drain(w);for(auto&arr:seen)for(int n:arr)ck(n==1,"all repeated-failure notices exactly once");
 ck(injected==9&&w.outcomes.size()==18&&receiverCalls==9&&w.receiverFailures==3&&w.lostNotified==6,"three growth points failed three times each");
 ck(w.set_lost_receiver([](const LostNotice&){}),"flag restored after repeated failed batches");
 printf("REPEATED_FAILURES bad_alloc=%d outcomes=%zu notices=%d receiver_throws=%zu\n",injected,w.outcomes.size(),receiverCalls,w.receiverFailures);
#else
 puts("SKIP repeated failure injection under ASan");
#endif
}


void nestedBatchThrows(){
#if !__has_feature(address_sanitizer)
 for(int rethrow=0;rethrow<2;++rethrow){
  World w(32);int a=env(w);std::array<uint64_t,3> ids{};std::array<DeliveryHandles,3> ds;
  std::array<std::array<int,3>,3> seen{};bool attempted=false;int depth=0,caught=0;
  w.set_lost_receiver([&](const LostNotice&n){
   ++depth;int j=0;while(ids[j]!=n.handoff)++j;int k=n.kind==Kind::CallbackEnd?0:n.kind==Kind::WorkEnd?1:2;++seen[j][k];
   ck(!w.set_lost_receiver([](const LostNotice&){}),"cannot replace at nested throw depth");
   if(!attempted){
    attempted=true;failAt=0;
    try{w.settle();ck(false,"nested batch should throw");}
    catch(const std::bad_alloc&){
     ++caught;failAt=-1;ck(!w.set_lost_receiver([](const LostNotice&){}),"nested throwing batch did not clear outer flag");
     if(rethrow){--depth;throw;}
    }
    failAt=-1;
   }
   --depth;
  });
  for(int j=0;j<3;++j){ids[j]=w.fault(a);ds[j]=*w.deliver(ids[j]);}
  auto c=*w.start_command(a);c.report();drain(w);auto ack=w.collect(a);
  for(auto&d:ds){d.callback={};d.work={};d.lease={};}ack[0].report();
  ck(w.settle()==8,"outer returns own eight after nested exception");failAt=-1;
  ck(caught==1&&w.outcomes.empty(),"nested ack remains uncompleted after injected failure");
  drain(w);drain(w);for(auto&arr:seen)for(int n:arr)ck(n==1,"nested-throw notices never replay");
  ck(depth==0&&w.receiverFailures==size_t(rethrow)&&w.lostNotified==size_t(9-rethrow)&&w.outcomes.size()==1,"nested throw restored accounting");
  ck(w.set_lost_receiver([](const LostNotice&){}),"outer flag finally cleared after nested throw");
  printf("NESTED_BAD_ALLOC caught=%d receiver_rethrows=%d notices=9 outcomes=1\n",caught,rethrow);
 }
#endif
}

int main(){rings();receiverAndGate();nested();successfulMutations();exhaustiveAlloc();repeatedFailures();nestedBatchThrows();printf("q150_checks=%d q150_failures=%d\n",checks,failures);return failures?1:0;}
