const fs=require('fs'),vm=require('vm');
const html=fs.readFileSync(process.argv[2],'utf8');
const src=/<script id="prototype-model">([\s\S]*?)<\/script>/.exec(html)[1];
const ctx={};ctx.globalThis=ctx;vm.createContext(ctx);vm.runInContext(src,ctx);
const res=ctx.IntegrationPrototype.runGuides();let steps=0,checks=0,fail=0,inv=0;
for(const g of res){steps+=g.steps.length;checks+=g.expectationsChecked;fail+=g.expectationsFailed;inv+=g.invariantErrors;
  console.log(`${g.guide}: steps=${g.steps.length} checks=${g.expectationsChecked} failed=${g.expectationsFailed} inv=${g.invariantErrors}`);
  for(const s of g.steps)if((s.expectation&&!s.expectation.ok)||s.errors.length)console.log(`  #${s.index} ${s.label} → ${s.result}${s.errors.length?' ⚠'+s.errors.join('；'):''}${s.expectation&&!s.expectation.ok?' ✗ '+s.expectation.why:''}`);}
console.log(`TOTAL steps=${steps} checks=${checks} failed=${fail} invariantErrors=${inv}`);
process.exitCode=fail||inv?1:0;
