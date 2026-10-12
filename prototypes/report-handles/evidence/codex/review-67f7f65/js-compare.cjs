const fs=require('fs'),vm=require('vm');
const html=fs.readFileSync('prototypes/result-delivery-contracts/integrated-v4.html','utf8');
vm.runInThisContext(html.match(/<script id="prototype-model">([\s\S]*?)<\/script>/)[1]);
const M=globalThis.IntegrationPrototype;
const g=M.runGuides();console.log('GUIDES',JSON.stringify(g.map(x=>({guide:x.guide,steps:x.steps.length,checks:x.expectationsChecked,failed:x.expectationsFailed,invariants:x.invariantErrors}))));
function run(w,a){const r=w.dispatch(a);if(r.errors.length)throw Error(JSON.stringify(r));return r;}
for(const unsafe of [false,true]){const w=M.newWorld();run(w,{type:'startCommand',env:'A',as:'t'});if(unsafe){run(w,{type:'setCleanupSafe',env:'A',safe:false});run(w,{type:'closeVisuals',env:'A'});}
run(w,{type:'postControl',kind:'result',body:{token:'@t',value:123,cancelled:!unsafe}});run(w,{type:'processControls'});run(w,{type:'collectReceipts',env:'A'});
console.log(unsafe?'JS unsafe_collect':'JS cancelled',JSON.stringify({value:w.envs.A.value,receipts:w.receipts.size,outcomes:w.outcomes.length,commands:w.commands.size}));}
