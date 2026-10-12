#include "../../report_model.hpp"
int main(){
#ifdef COPY
 rh::ReportHandle a; rh::ReportHandle b=a;
#else
 auto l=std::make_shared<rh::Ledger>(1); auto r=*l->reserve(rh::Kind::Result,1); rh::ReportHandle h(l,r);
#endif
}
