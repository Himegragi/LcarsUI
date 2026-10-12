// THROWAWAY scenarios for report_model.hpp (ADR 0003 Q135-Q143). Console only.
#include "report_model.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <type_traits>

#if defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#    define RH_ASAN 1  // any sanitizer that owns operator new
#  endif
#endif

// Allocation counter for "report path does not allocate" (disabled under ASan/TSan, which own operator new).
static thread_local std::size_t g_allocs = 0;
static thread_local int g_failAfter = -1;  // >=0: that many allocations succeed, then one throws bad_alloc
#ifndef RH_ASAN
void* operator new(std::size_t n) {
    if (g_failAfter == 0) { g_failAfter = -1; throw std::bad_alloc(); }
    if (g_failAfter > 0) --g_failAfter;
    ++g_allocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#endif

using namespace rh;
template <class W> constexpr bool can_fill_directly = requires(W& w, Ledger::Ref r) { w.ledger().fill(r, Payload{}); };
static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what) { std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what); ok ? ++g_pass : ++g_fail; }
static void inv(World& w, const char* at) {
    auto e = w.invariants();
    for (auto& s : e) std::printf("  INVARIANT(%s): %s\n", at, s.c_str());
    check(e.empty(), at);
}
static void settle_all(World& w) { while (w.settle()) {} }
// Q149: callback/work/lease obligations need a registered lost receiver.
static void quiet(World& w) { w.set_lost_receiver([](const LostNotice&) {}); }

int main() {
    std::printf("== S1 型別：只能移動、不能複製、不能由公開資料拼出 (Q140/Q142) ==\n");
    check(!std::is_copy_constructible_v<ReportHandle> && !std::is_copy_assignable_v<ReportHandle>, "不可複製");
    check(std::is_nothrow_move_constructible_v<ReportHandle> && std::is_nothrow_move_assignable_v<ReportHandle>, "可移動且不拋例外");
    check(!std::is_constructible_v<ReportHandle, std::weak_ptr<Ledger>, Ledger::Ref>, "外部無法由 ledger＋編號建構把手");
    { ReportHandle empty; check(empty.report({1}) == Reject::Empty, "預設把手回報 → 空把手（比命令先到的結果無從送出）"); }

    std::printf("== S2 預留不足就不承擔 (Q135) ==\n");
    {
        World w(3); quiet(w); auto a = *w.add_env("A", Contract::Handover);
        std::vector<ReportHandle> hs;
        for (int i = 0; i < 3; ++i) hs.push_back(*w.start_command(a));
        auto r = w.start_command(a);
        check(!r && w.refusedObligations == 1 && w.commands.size() == 3 && w.receipts.size() == 3, "第 4 個命令被拒，沒有留下命令或回條");
        auto h = w.fault(a);
        check(!w.deliver(h) && w.ledger().live() == 3, "交接需要 4 個位置：不足時全部不承擔，已預留的退回");
        for (auto& x : hs) x.report({7});
        settle_all(w);
        check(w.ledger().live() == 0 && w.start_command(a).has_value(), "回報結算後位置歸還，可再承擔");
        inv(w, "S2 結束");
    }

    std::printf("== S3 回報路徑不配置記憶體 (Q135) ==\n");
    {
#ifdef RH_ASAN
        std::printf("  SKIP ASan 建置下不替換 operator new\n");
#else
        World w(4); auto a = *w.add_env("A", Contract::Handover);
        auto h1 = *w.start_command(a); auto h2 = *w.start_command(a);
        std::size_t used = 0;
        std::thread t([&] { const auto before = g_allocs; h1.report({1}); { ReportHandle dead = std::move(h2); } used = g_allocs - before; });
        t.join();
        check(used == 0, "背景執行緒回報與丟棄把手：0 次配置");
        settle_all(w); inv(w, "S3 結束");
#endif
    }

    std::printf("== S4 多執行緒：轉交、回報、丟棄、重複 (Q140/Q142/Q143a) ==\n");
    {
        constexpr int N = 400, T = 8;
        World w(64); auto a = *w.add_env("A", Contract::Handover);
        std::atomic<int> rejectedEmpty{0}, accepted{0}, dropped{0};
        int started = 0, refused = 0;
        std::vector<std::thread> pool;
        std::mutex qm; std::vector<std::pair<int, ReportHandle>> queue; std::atomic<bool> stop{false};
        for (int t = 0; t < T; ++t) pool.emplace_back([&] {
            for (;;) {
                std::pair<int, ReportHandle> job{-1, ReportHandle{}};
                { std::lock_guard g(qm); if (!queue.empty()) { job = std::move(queue.back()); queue.pop_back(); } }
                if (job.first < 0) { if (stop.load()) return; std::this_thread::yield(); continue; }
                ReportHandle mine = std::move(job.second);  // second transfer: worker-local
                if (job.first % 10 == 0) { dropped++; continue; }  // provider forgets: handle destroyed here
                if (mine.report({job.first}) == Reject::None) accepted++;
                if (mine.report({-1}) == Reject::Empty) rejectedEmpty++;  // reuse after use
            }
        });
        int i = 0;
        while (i < N) {
            if (auto h = w.start_command(a)) { std::lock_guard g(qm); queue.emplace_back(i++, std::move(*h)); ++started; }
            else { ++refused; w.settle(); }
        }
        stop = true; for (auto& t : pool) t.join();
        settle_all(w);
        std::size_t done = 0, lost = 0;
        for (auto& [id, c] : w.commands) { if (c.terminal == "已完成") ++done; if (c.terminal == "提供者失效") ++lost; }
        check(started == N && done == static_cast<std::size_t>(accepted.load()) && lost == static_cast<std::size_t>(dropped.load()), "每個命令剛好一個終態：完成數＝受理數，提供者失效＝丟棄數");
        check(rejectedEmpty.load() == accepted.load(), "用過的把手再回報 → 空把手");
        check(w.ledger().live() == 0 && refused > 0, "容量滿時拒絕新命令，最後預留全部歸還");
        check(w.providerLost == static_cast<std::size_t>(dropped.load()), "Q143a：丟棄的結果把手自動結算為提供者失效");
        std::printf("  started=%d refused=%d accepted=%d dropped=%d\n", started, refused, accepted.load(), dropped.load());
        inv(w, "S4 結束");
    }

    std::printf("== S5 交接：改派撤權、回報者遺失 (Q140/Q141/Q143b) ==\n");
    {
        World w(16); quiet(w); auto a = *w.add_env("A", Contract::Handover);
        auto h = w.fault(a);
        auto d1 = *w.deliver(h);
        check(w.reassign(h), "改派：撤銷舊確認資格");
        check(d1.ack.report() == Reject::Revoked && w.ledger().rejected(Reject::Revoked) == 1, "舊接管者確認 → 已撤權，並記診斷");
        std::thread([cb = std::move(d1.callback), wk = std::move(d1.work)]() mutable { cb.report(); wk.report(); }).join();
        { ReportHandle lostLease = std::move(d1.lease); }  // lease reporter lost
        settle_all(w);
        auto& x = w.handoffs.at(h);
        check(!x.cb.active && !x.work.active && x.lease.active && x.lease.lost && w.lost_parts().size() == 1, "租用把手遺失：不推定已歸還，標記並可查詢");
        auto d2 = *w.deliver(h);
        check(!d2.lease.valid(), "舊租用未結清：新指派不另發租用把手");
        d2.callback.report(); d2.work.report(); d2.ack.report();
        settle_all(w);
        check(w.handoffs.count(h) == 1, "已確認但遺失的租用未處置 → 交接不釋放");
        check(w.declare_ended(h, Kind::LeaseEnd) == Declare::Done && w.handoffs.count(h) == 0, "Q144：擁有者宣告租用已結束 → 交接釋放");
        inv(w, "S5 結束");
    }

    std::printf("== S6 交付成立點 (Q136/Q137) ==\n");
    {
        World w(16);
        check(!w.add_env("D", std::nullopt) && !w.add_env("E", static_cast<Contract>(7)), "沒寫或不合法的承接成立點 → 拒絕");
        auto a = *w.add_env("A", Contract::Handover), c = *w.add_env("C", Contract::ExplicitAck);
        auto ha = *w.start_command(a), c1 = *w.start_command(c), c2 = *w.start_command(c);
        ha.report({1}); c1.report({2}); c2.report({3});
        settle_all(w);
        w.close(a); w.close(c);
        check(!w.envs[a].ended && !w.envs[c].ended, "關閉不等於交付：兩者收尾中");
        auto none = w.collect(a);
        check(none.empty() && w.envs[a].ended, "A 交出即承接 → 結束");
        auto acks = w.collect(c);
        check(acks.size() == 2 && !w.envs[c].ended, "C 每份結果各一個確認把手，未確認前收尾中");
        std::thread([k = std::move(acks[0])]() mutable { k.report(); }).join();
        settle_all(w);
        check(!w.envs[c].ended && w.outcomes.size() == 2, "確認一份：C 仍收尾中");
        ReportHandle moved = std::move(acks[1]);
        check(acks[1].report() == Reject::Empty, "被移走的把手 → 空把手");
        moved.report(); settle_all(w);
        check(w.envs[c].ended && w.outcomes.size() == 3, "兩份都確認 → C 結束");
        inv(w, "S6 結束");
    }

    std::printf("== S7 擁有者明確放棄 (Q138) ==\n");
    {
        World w(16); auto c = *w.add_env("C", Contract::ExplicitAck);
        auto k1 = *w.start_command(c); auto k2 = *w.start_command(c);
        k1.report({1}); settle_all(w);
        w.close(c);
        auto acks = w.collect(c);
        w.set_cleanup_safe(c, false);
        check(!w.abandon(c) && w.receipts.size() == 2, "收尾不安全 → 拒絕放棄");
        w.set_cleanup_safe(c, true);
        auto n = w.abandon(c);
        check(n && *n == 1 && w.outcomes.back().outcome == "已放棄" && !w.envs[c].ended, "放棄已終態結果並留紀錄；仍執行的命令不受影響");
        check(acks[0].report() == Reject::Revoked, "已放棄結果的舊確認把手 → 已撤權");
        k2.report({2}); settle_all(w);
        auto acks2 = w.collect(c);
        acks2[0].report(); settle_all(w);
        check(w.envs[c].ended && w.outcomes.back().outcome == "已交付", "晚到結果仍可正常交付；放棄與交付分開紀錄");
        inv(w, "S7 結束");
    }

    std::printf("== S8 世界先銷毀，背景執行緒才回報 ==\n");
    {
        ReportHandle survivor, dropper;
        { World w(4); auto a = *w.add_env("A", Contract::Handover); survivor = *w.start_command(a); dropper = *w.start_command(a); }
        Reject r{};
        std::thread([&] { r = survivor.report({1}); { ReportHandle d = std::move(dropper); } }).join();
        check(r == Reject::WorldGone, "回報 → 世界已銷毀；丟棄把手不崩潰");
    }

    std::printf("== S9 非擁有者執行緒不能改狀態 ==\n");
    {
        World w(4); auto a = *w.add_env("A", Contract::Handover);
        bool threw = false;
        std::thread([&] { try { w.settle(); } catch (const std::logic_error&) { threw = true; } }).join();
        check(threw && w.ownerThreadViolations == 1, "背景執行緒呼叫 settle → 拒絕");
        (void)a;
    }

    std::printf("== S10 壓力：隨機交錯 (ASan 用) ==\n");
    {
        World w(32); auto a = *w.add_env("A", Contract::Handover), c = *w.add_env("C", Contract::ExplicitAck);
        std::mt19937 rng(12345);
        std::vector<std::thread> ts; std::size_t issued = 0;
        for (int round = 0; round < 200; ++round) {
            std::vector<ReportHandle> batch;
            for (int k = 0; k < 6; ++k) if (auto h = w.start_command(rng() % 2 ? a : c)) { batch.push_back(std::move(*h)); ++issued; }
            for (auto& h : batch) ts.emplace_back([hh = std::move(h), drop = rng() % 7 == 0, v = static_cast<int>(rng() % 100)]() mutable { if (!drop) hh.report({v}); });
            if (round % 3 == 0) { for (auto& t : ts) t.join(); ts.clear(); }
            w.settle();
            w.collect(a);
            for (auto& k : w.collect(c)) if (rng() % 2) k.report();  // the other half drop their ack handle
        }
        for (auto& t : ts) t.join();
        settle_all(w);
        for (int k = 0; k < 4; ++k) { for (auto& x : w.collect(c)) x.report(); settle_all(w); w.collect(a); }
        check(w.receipts.empty() && w.ledger().live() == 0, "全部結果最終交付，預留全部歸還");
        std::printf("  issued=%zu outcomes=%zu providerLost=%zu\n", issued, w.outcomes.size(), w.providerLost);
        check(w.outcomes.size() == issued, "每個命令剛好一筆結清紀錄");
        inv(w, "S10 結束");
    }

    std::printf("== S11 Codex 審查 67f7f65 的反例回歸 ==\n");
    {
        constexpr bool worldCopyable = std::is_copy_constructible_v<World> || std::is_copy_assignable_v<World> || std::is_move_constructible_v<World>;
        check(!worldCopyable, "F2：World 不可複製、不可移動（避免兩個 World 共用一本帳）");
        constexpr bool canFillDirectly = can_fill_directly<World>;
        check(!canFillDirectly, "F3：外部拿不到可寫的 Ledger，回報只能經過把手");
    }
    {
        World w(16); quiet(w); auto a = *w.add_env("A", Contract::Handover);
        auto h = w.fault(a); auto d = *w.deliver(h);
        d.callback.report(); d.work.report(); d.lease.report(); settle_all(w);
        auto again = w.deliver(h);
        check(!again, "F1：舊確認把手還沒用掉時，拒絕再次遞送");
        check(d.ack.report() == Reject::None, "F1：舊確認把手仍是唯一有效確認");
        settle_all(w);
        check(w.handoffs.count(h) == 0, "F1：確認後交接釋放");
        inv(w, "F1 結束");
    }
    {
        World w(4); auto a = *w.add_env("A", Contract::Handover);
        auto k = *w.start_command(a); k.report({123, true}); settle_all(w);
        check(w.envs[a].value == 0, "F4：已取消的結果不寫回環境");
        auto k2 = *w.start_command(a);
        w.set_cleanup_safe(a, false); w.close(a); k2.report({5}); settle_all(w);
        w.collect(a);
        check(w.receipts.size() == 2 && w.outcomes.empty(), "F5：收尾不安全時不交出結果");
        w.set_cleanup_safe(a, true); w.collect(a);
        check(w.receipts.empty() && w.envs[a].ended, "F5：宣告安全後交付並結束");
        inv(w, "F4/F5 結束");
    }
    {
        World w(4); auto a = *w.add_env("A", Contract::Handover);
        auto k = *w.start_command(a); k.report({1});
        check(k.report({2}) == Reject::Empty && w.ledger().rejected(Reject::Empty) == 1, "F6：同一把手回報兩次也記入診斷");
        settle_all(w);
    }
    {
        World w(4);
        std::thread t1([&] { try { w.settle(); } catch (const std::logic_error&) {} });
        std::thread t2([&] { try { w.settle(); } catch (const std::logic_error&) {} });
        t1.join(); t2.join();
        check(w.ownerThreadViolations == 2, "兩個背景執行緒同時違規：計數正確（atomic）");
    }
#ifndef RH_ASAN
    {
        bool allOk = true; int threw = 0;
        for (int k = 0; k < 40; ++k) {
            World w(4); auto a = *w.add_env("A", Contract::Handover);
            g_failAfter = k;
            try { auto h = w.start_command(a); if (h) h->report({1}); } catch (const std::bad_alloc&) { ++threw; }
            g_failAfter = -1;
            settle_all(w);
            if (!w.invariants().empty() || w.ledger().live() != 0) { allOk = false; std::printf("  F7 k=%d live=%zu inv=%zu\n", k, w.ledger().live(), w.invariants().size()); }
        }
        check(allOk && threw > 0, "F7：建立命令途中配置失敗，不留下預留或半套義務");
    }
    {
        bool allOk = true; int threw = 0;
        for (int k = 0; k < 40; ++k) {
            World w(16); quiet(w); auto a = *w.add_env("A", Contract::Handover);
            auto h = w.fault(a);
            auto d = *w.deliver(h);
            { ReportHandle c = std::move(d.callback), wk = std::move(d.work); }
            g_failAfter = k;
            try { w.settle(); } catch (const std::bad_alloc&) { ++threw; }
            g_failAfter = -1;
            settle_all(w);
            auto& x = w.handoffs.at(h);
            const bool ok = x.cb.lost && x.work.lost && x.cb.active && x.work.active && w.lost_parts().size() == 2 && w.lostNotified == 2 && w.invariants().empty();
            if (!ok) { allOk = false; std::printf("  F8 k=%d cbLost=%d workLost=%d lost=%zu notified=%zu inv=%zu\n", k, x.cb.lost, x.work.lost, w.lost_parts().size(), w.lostNotified, w.invariants().size()); }
        }
        check(allOk, "F8：配置失敗注入後，遺失回報都被標記、可查詢、帳目一致");
        check(threw == 0, "Q145 之後：結算遺失回報的路徑不再配置記憶體，沒有可失敗的配置點");
    }
#else
    std::printf("  SKIP F7/F8 配置失敗注入需要替換 operator new\n");
#endif


    std::printf("== S12 擁有者處置遺失回報 (Q144/Q145) ==\n");
    {
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        std::vector<LostNotice> got; std::thread::id where{};
        w.set_lost_receiver([&](const LostNotice& n) { got.push_back(n); where = std::this_thread::get_id(); });
        auto h = w.fault(a); auto d = *w.deliver(h);
        std::thread([cb = std::move(d.callback), ls = std::move(d.lease), wk = std::move(d.work)]() mutable { wk.report(); }).join();
        d.ack.report(); settle_all(w);
        check(got.size() == 2 && where == std::this_thread::get_id(), "Q145：兩則遺失通知，都在擁有者執行緒送達");
        check(w.lost_parts().size() == 2 && w.handoffs.count(h) == 1, "Q145：可查詢目前遺失項；交接未釋放");
        check(w.declare_ended(h, Kind::WorkEnd) == Declare::NotLost, "Q144：已正常回報的項目不能宣告");
    }
    {
        World w(16); quiet(w); auto a = *w.add_env("A", Contract::Handover);
        auto h = w.fault(a); auto d = *w.deliver(h);
        check(w.declare_ended(h, Kind::CallbackEnd) == Declare::NotLost && w.handoffs.at(h).cb.active, "Q144：把手仍有人持有的正常項目不能宣告");
        check(d.callback.report() == Reject::None, "Q144：持有者之後仍能正常回報");
        d.work.report(); d.lease.report(); d.ack.report(); settle_all(w);
        inv(w, "S12 未遺失項目");
    }
    {
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        std::vector<LostNotice> got; std::thread::id where{};
        w.set_lost_receiver([&](const LostNotice& n) { got.push_back(n); where = std::this_thread::get_id(); });
        auto h = w.fault(a); auto d = *w.deliver(h);
        std::thread([cb = std::move(d.callback), ls = std::move(d.lease), wk = std::move(d.work)]() mutable { wk.report(); }).join();
        d.ack.report(); settle_all(w);
        check(w.declare_ended(h, Kind::HandoffAck) == Declare::Unknown && w.declare_ended(999, Kind::CallbackEnd) == Declare::Unknown, "Q144：非回呼／工作／租用或不存在的交接 → 拒絕");
        check(w.declare_ended(h, Kind::CallbackEnd) == Declare::Done && w.handoffs.count(h) == 1 && w.lost_parts().size() == 1, "Q144：逐項宣告回呼結束；租用仍遺失，交接保留");
        check(w.declare_ended(h, Kind::CallbackEnd) == Declare::NotLost, "Q144：同一項不能宣告兩次");
        check(w.declare_ended(h, Kind::LeaseEnd) == Declare::Done && w.handoffs.count(h) == 0, "Q144：最後一項宣告後交接釋放");
        check(w.declarations.size() == 2 && w.reportedEnds == 1, "Q144：擁有者宣告結束與正常回報分開記錄");
        inv(w, "S12a 結束");
    }
    {
        World w(16); quiet(w); auto a = *w.add_env("A", Contract::Handover);
        auto h = w.fault(a); auto d1 = *w.deliver(h);
        w.reassign(h);
        { ReportHandle gone = std::move(d1.callback); }
        d1.work.report(); settle_all(w);
        check(!w.deliver(h), "改派等待舊回呼結束：回呼把手遺失時無法再遞送");
        check(w.declare_ended(h, Kind::CallbackEnd) == Declare::Done, "擁有者宣告舊回呼已結束");
        auto d2 = w.deliver(h);
        check(d2.has_value() && !d2->lease.valid(), "宣告後改派繼續：新指派取得把手（舊租用仍在，不另發）");
        d2->callback.report(); d2->work.report(); d2->ack.report(); d1.lease.report(); settle_all(w);
        check(w.handoffs.count(h) == 0, "新接管者完成後交接釋放");
        inv(w, "S12b 結束");
    }
    {
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        Declare inside = Declare::Done; std::uint64_t h = 0;
        w.set_lost_receiver([&](const LostNotice& n) { inside = w.declare_ended(n.handoff, n.kind); });
        h = w.fault(a); auto d = *w.deliver(h);
        { ReportHandle gone = std::move(d.lease); }
        settle_all(w);
        check(inside == Declare::Done && !w.handoffs.at(h).lease.lost && !w.handoffs.at(h).lease.active, "Q150：批次結束後才通知，接收者可直接宣告結束");
        check(w.declare_ended(h, Kind::LeaseEnd) == Declare::NotLost, "已由接收者宣告的項目不能再宣告");
        d.callback.report(); d.work.report(); d.ack.report(); settle_all(w);
        inv(w, "S12c 結束");
    }
    {
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        int calls = 0;
        w.set_lost_receiver([&](const LostNotice&) { ++calls; throw std::runtime_error("receiver bug"); });
        auto h = w.fault(a); auto d = *w.deliver(h);
        { ReportHandle gone = std::move(d.work); }
        settle_all(w);
        check(calls == 1 && w.receiverFailures == 1 && w.handoffs.at(h).work.lost && w.lost_parts().size() == 1, "接收者拋例外：記錄失敗、不重送，遺失狀態仍可查詢");
        (void)h;
    }
    {
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        auto h = w.fault(a);
        check(!w.deliver(h) && w.refusedNoReceiver == 1 && w.ledger().live() == 0, "Q149：未登記接收者 → 不受理遞送，不留預留");
        check(!w.set_lost_receiver(nullptr) && !w.deliver(h), "Q149：空接收者不算登記");
        quiet(w);
        check(w.deliver(h).has_value(), "Q149：登記後可遞送");
    }
    {
        // Q150: notices arrive after the batch, so the receiver may change state; the batch it came from is already done.
        World w(32); auto a = *w.add_env("A", Contract::Handover);
        std::uint64_t h = 0; std::size_t threw = 0, calls = 0;
        w.set_lost_receiver([&](const LostNotice& n) {
            ++calls;
            auto attempt = [&](auto f) { try { f(); } catch (...) { ++threw; } };
            attempt([&] { w.settle(); });
            attempt([&] { w.collect(a); });
            attempt([&] { w.abandon(a); });
            attempt([&] { w.reassign(h); });
            attempt([&] { w.start_command(a); });
            attempt([&] { check(w.declare_ended(n.handoff, n.kind) == Declare::Done, "Q150：接收者內宣告結束"); });
            attempt([&] { check(!w.set_lost_receiver([](const LostNotice&) {}), "通知送出中不可更換接收者"); });
            attempt([&] { w.close(a); });
            attempt([&] { w.set_cleanup_safe(a, true); });
        });
        std::vector<ReportHandle> later;
        for (int i = 0; i < 3; ++i) later.push_back(*w.start_command(a));
        h = w.fault(a); auto d = *w.deliver(h);
        { ReportHandle gone = std::move(d.callback); }
        for (int i = 0; i < 3; ++i) later[i].report({i + 1});
        bool outerThrew = false;
        try { settle_all(w); } catch (...) { outerThrew = true; }
        std::size_t done = 0; for (auto& [id, c] : w.commands) if (c.terminal == "已完成") ++done;
        check(!outerThrew && calls == 1 && threw == 0, "Q150：接收者呼叫任何方法都被受理，外層結算不受影響");
        check(done == 3 && w.envs[a].closed && !w.handoffs.at(h).ack, "三個結果各結算一次；接收者的關閉與改派生效");
        inv(w, "S12d 結束");
    }
    std::printf("== S13 拒絕原因、診斷上限、通知時機 (Q146/Q147/Q150) ==\n");
    {
        check(std::string(name(Reject::Empty)) == "把手已用過或為空" && std::string(name(Reject::Revoked)) == "已撤權或已失效" && std::string(name(Reject::WorldGone)) == "所屬環境已結束", "Q146：三個固定大類");
    }
    {
        World w(8, 3); auto a = *w.add_env("A", Contract::Handover);
        std::vector<ReportHandle> hs;
        for (int i = 0; i < 5; ++i) hs.push_back(*w.start_command(a));
        for (auto& x : hs) { x.report({1}); x.report({2}); }
        const auto d = w.ledger().recent_rejects();
        check(w.ledger().rejected(Reject::Empty) == 5, "Q147：計數準確（5 次）");
        check(d.size() == 3 && d[0].seq == 2 && d[2].seq == 4 && d[2].reason == Reject::Empty, "Q147：詳細紀錄只留最新 3 筆，由舊到新");
        World none(4, 0); auto b = *none.add_env("B", Contract::Handover);
        auto k = *none.start_command(b); k.report(); k.report();
        check(none.ledger().rejected(Reject::Empty) == 1 && none.ledger().recent_rejects().empty(), "Q147：N=0 時只計數");
        settle_all(w); settle_all(none);
    }
#ifndef RH_ASAN
    {
        World w(8, 4); auto a = *w.add_env("A", Contract::Handover);
        auto k = *w.start_command(a);
        const auto before = g_allocs;
        k.report(); for (int i = 0; i < 20; ++i) k.report();
        check(g_allocs == before, "Q147：記錄拒絕細節 0 次配置（環形空間事先配置）");
        settle_all(w);
    }
    {
        // Notices for items already completed must still go out when a later item in the same batch throws.
        bool allOk = true; int threw = 0;
        for (int k = 0; k < 12; ++k) {
            World w(16); auto c = *w.add_env("C", Contract::ExplicitAck);
            std::size_t got = 0; w.set_lost_receiver([&](const LostNotice&) { ++got; });
            auto h = w.fault(c); auto d = *w.deliver(h);        // slots 0-3: callback, work, lease, ack
            auto cmd = *w.start_command(c); cmd.report({1}); settle_all(w);
            auto acks = w.collect(c);                           // result ack in slot 4 (after the handoff slots)
            { ReportHandle x = std::move(d.callback), y = std::move(d.lease); }
            acks[0].report();                                   // record() of this ack allocates
            g_failAfter = k;
            try { w.settle(); } catch (const std::bad_alloc&) { ++threw; }
            g_failAfter = -1;
            settle_all(w);
            const bool ok = got == 2 && w.lostNotified == 2 && w.outcomes.size() == 1 && w.invariants().empty();
            if (!ok) { allOk = false; std::printf("  S13 k=%d got=%zu outcomes=%zu inv=%zu\n", k, got, w.outcomes.size(), w.invariants().size()); }
            (void)h;
        }
        check(allOk && threw > 0, "Q150：批次中途配置失敗，已完成項目的通知仍各送一次，重試不重送");
    }
#endif
    {
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        std::size_t calls = 0;
        w.set_lost_receiver([&](const LostNotice&) { if (++calls == 1) throw std::runtime_error("receiver bug"); });
        auto h = w.fault(a); auto d = *w.deliver(h);
        { ReportHandle x = std::move(d.callback), y = std::move(d.work); }
        settle_all(w);
        check(calls == 2 && w.receiverFailures == 1 && w.lostNotified == 1, "接收者第一則拋例外：計數，其餘通知照送");
        (void)h;
    }
#ifndef RH_ASAN
    {
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        std::size_t seen = 0;
        w.set_lost_receiver([&](const LostNotice&) { ++seen; });
        auto h = w.fault(a); auto d = *w.deliver(h);
        { ReportHandle c = std::move(d.callback), wk = std::move(d.work), ls = std::move(d.lease); }
        const auto before = g_allocs; settle_all(w); const auto used = g_allocs - before;
        check(seen == 3 && used == 0, "Q145：偵測遺失並送出通知，0 次配置");
        (void)h;
    }
#endif

    {
        // Codex 930c49b: a moved-from handle still belongs to a live World, so its rejection must be counted.
        World w(4, 4); auto a = *w.add_env("A", Contract::Handover);
        auto source = *w.start_command(a);
        auto destination = std::move(source);
        check(source.report() == Reject::Empty && w.ledger().rejected(Reject::Empty) == 1 && w.ledger().recent_rejects().size() == 1, "Q147：被移走的空殼回報也計數");
        ReportHandle other = *w.start_command(a);
        other = std::move(destination);  // the old obligation of `other` is dropped; destination becomes a shell
        check(destination.report() == Reject::Empty && w.ledger().rejected(Reject::Empty) == 2, "Q147：移動指派後的空殼回報也計數");
        ReportHandle never;
        check(never.report() == Reject::Empty && w.ledger().rejected(Reject::Empty) == 2, "從未綁定帳本的把手：回報被拒但無處計數（已知限制）");
        other.report(); settle_all(w);
        inv(w, "S13 空殼計數");
    }
    std::printf("== S14 暫停／恢復與接收途徑（自 JS 模型移植）==\n");
    {   // JS guide 2: shared untrusted service; recovery needs an explicit isolation proof
        World w(32); quiet(w);
        auto s1 = w.add_service(); auto rA = *w.add_route(), rB = *w.add_route();
        auto A = *w.add_env("A", Contract::Handover, rA, {s1}), B = *w.add_env("B", Contract::Handover, rB, {s1});
        w.lose_route(rB);
        check(w.envs[B].state == EnvState::Paused, "B 途徑失效 → 暫停");
        auto h = w.fail_env(A, s1); auto d = *w.deliver(h);
        check(w.envs[A].state == EnvState::Faulted && w.envs[B].needsProof, "A 故障、共享服務不可信 → B 需要隔離證明");
        auto rB2 = *w.add_route();
        check(!w.begin_resume(B, rB2) && std::string(w.resume_blocker(B, rB2)) == "依賴不可信的服務", "新途徑不能修好不可信的服務");
        w.declare_isolation(B, Isolation::Unknown);
        check(std::string(w.resume_blocker(B, rB2)) == "隔離未知", "隔離未知 → 拒絕");
        w.bind_new_service(B, s1); w.declare_isolation(B, Isolation::Undeclared);
        check(!w.begin_resume(B, rB2) && std::string(w.resume_blocker(B, rB2)) == "共享故障後尚未證實隔離", "換新服務不等於證實隔離");
        w.declare_isolation(B, Isolation::Proven);
        auto at = w.begin_resume(B, rB2);
        check(at && w.commit_resume(*at) == Commit::Committed && w.envs[B].state == EnvState::Running && w.handoffs.count(h) == 1, "證實隔離後恢復；A 的交接仍未結清");
        check(w.start_command(B).has_value() && !w.start_command(A).has_value(), "B 可接新工作；故障的 A 不行");
        d.callback.report(); d.work.report(); d.lease.report(); d.ack.report(); settle_all(w);
        check(w.handoffs.count(h) == 0, "A 的交接結清");
        inv(w, "S14a 結束");
    }
    {   // proof is voided by a later shared fault
        World w(16); quiet(w);
        auto s1 = w.add_service();
        auto A = *w.add_env("A", Contract::Handover, std::nullopt, {s1}), B = *w.add_env("B", Contract::Handover, std::nullopt, {s1});
        w.declare_isolation(B, Isolation::Proven);
        w.fail_env(A, s1);
        check(w.envs[B].state == EnvState::Paused && w.envs[B].isolation == Isolation::Undeclared, "執行中的 B 被暫停，先前的隔離證明作廢");
        inv(w, "S14 證明作廢");
    }
    {   // pause: cancellation requests, no new work, held results
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        auto keep = *w.start_command(a, true), stop = *w.start_command(a);
        w.lose_route(w.envs[a].route);
        std::size_t req = 0; for (auto& [id, c] : w.commands) if (c.cancelRequested) ++req;
        check(req == 1 && w.cancelRequests == 1, "暫停：只有未宣告可繼續的命令收到取消請求（不是完成）");
        check(!w.start_command(a) && w.refusedNotRunning == 1, "暫停中不接新工作");
        keep.report({7}); stop.report({0, true}); settle_all(w);
        check(w.envs[a].value == 0 && w.collect(a).empty() && w.receipts.size() == 2, "暫停中結果只結算、不套用、不可交出");
        inv(w, "S14 暫停");
    }
    {   // JS guide 4: cancel vs commit, first legal settlement wins
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        auto c1 = *w.start_command(a, true), c2 = *w.start_command(a, true);
        w.lose_route(w.envs[a].route);
        c1.report({1}); c2.report({2}); settle_all(w);
        auto r2 = *w.add_route(); auto a1 = *w.begin_resume(a, r2);
        check(w.resume_step(a1) == 2 && w.envs[a].state == EnvState::Resuming && w.envs[a].value == 2, "本批處理完 ≠ 已提交");
        check(w.cancel_resume(a1) == Commit::Cancelled && w.commit_resume(a1) == Commit::AlreadyEnded && w.envs[a].state == EnvState::Paused, "取消先到：提交回報已結束");
        auto a2 = *w.begin_resume(a, r2);
        check(w.commit_resume(a2) == Commit::Committed && w.cancel_resume(a2) == Commit::TooLate, "提交先到：取消太晚");
        check(w.resume_step(a1) == 0 && w.staleAttempts == 1, "舊嘗試已過期");
        check(w.collect(a).empty() && w.outcomes.size() == 2, "兩份結果交付");
        inv(w, "S14 取消對提交");
    }
    {   // JS guide 6: late results are not added to the frozen batch
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        std::vector<ReportHandle> c; for (int i = 0; i < 4; ++i) c.push_back(*w.start_command(a, true));
        w.lose_route(w.envs[a].route);
        c[0].report({11}); settle_all(w);
        auto r2 = *w.add_route(); auto at = *w.begin_resume(a, r2);
        c[1].report({22}); settle_all(w);
        check(w.envs[a].attempt->batch.size() == 1, "晚到的 c2 不加入本批");
        w.resume_step(at);
        check(w.commit_resume(at) == Commit::Committed && w.envs[a].value == 11, "提交：c2 仍保留未套用");
        check(w.process_pending(a) == 1 && w.envs[a].value == 22 && w.process_pending(a) == 0, "一般處理輪處置 c2 一次");
        w.lose_route(r2);
        c[2].report({33}); settle_all(w);
        auto r3 = *w.add_route(); auto at2 = *w.begin_resume(a, r3);
        c[3].report({44}); settle_all(w);
        w.resume_step(at2);
        w.lose_route(r3);
        check(w.envs[a].state == EnvState::Paused && w.commit_resume(at2) == Commit::AlreadyEnded, "提交前途徑失效：嘗試失效、不重新開放");
        check(w.process_pending(a) == 0 && w.receipts.rbegin()->second.disp.empty() && w.receipts.rbegin()->second.terminal == "已完成", "暫停中一般處理輪被拒，c4 仍保留");
        inv(w, "S14 晚到結果");
    }
    {   // bounded route table
        World w(8); auto a = *w.add_env("A", Contract::Handover);
        bool ok = true;
        for (int i = 0; i < 10; ++i) {
            w.lose_route(w.envs[a].route);
            auto r = w.add_route(); auto at = r ? w.begin_resume(a, *r) : std::nullopt;
            ok = ok && at && w.commit_resume(*at) == Commit::Committed && w.routes.size() <= World::kRouteCap;
        }
        check(ok, "反覆失效／安裝／恢復：途徑表不超過上限");
        for (int i = 0; i < 10; ++i) w.add_route();
        check(w.routes.size() == World::kRouteCap && w.refusedRegistry >= 1, "全部有效時拒絕安裝");
        inv(w, "S14 途徑表");
    }
    std::printf("== S15 暫停／恢復 × 回報把手 ==\n");
    {   // dropped result handle while paused: provider-lost result is held, then disposed by recovery
        World w(16); auto a = *w.add_env("A", Contract::ExplicitAck);
        auto k = *w.start_command(a, true);
        w.lose_route(w.envs[a].route);
        { ReportHandle gone = std::move(k); }
        settle_all(w);
        check(w.receipts.begin()->second.terminal == "提供者失效" && w.receipts.begin()->second.disp.empty(), "暫停中提供者失效：終態已定，處置保留");
        auto at = *w.begin_resume(a, *w.add_route()); w.resume_step(at); w.commit_resume(at);
        check(w.receipts.begin()->second.disp == "無結果", "恢復時處置為無結果");
        auto acks = w.collect(a);
        check(acks.size() == 1, "處置後才發確認把手");
        if (!acks.empty()) acks[0].report();
        settle_all(w);
        check(w.receipts.empty(), "逐筆確認後交付");
        inv(w, "S15 提供者失效");
    }
    {   // abandon while paused; recovery skips the abandoned result
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        auto k1 = *w.start_command(a, true), k2 = *w.start_command(a, true);
        w.lose_route(w.envs[a].route);
        k1.report({1}); k2.report({2}); settle_all(w);
        check(w.abandon(a) == 2u && w.receipts.empty(), "暫停中擁有者可明確放棄已終態結果");
        auto at = *w.begin_resume(a, *w.add_route());
        check(w.envs[a].attempt->batch.empty() && w.commit_resume(at) == Commit::Committed && w.envs[a].value == 0, "已放棄的結果不進入本批、不套用");
        inv(w, "S15 放棄");
    }
    {   // close during resume: attempt invalidated, held results become 不適用; unsafe close keeps them
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        auto k1 = *w.start_command(a, true), k2 = *w.start_command(a, true);
        w.lose_route(w.envs[a].route);
        k1.report({1}); k2.report({2}); settle_all(w);
        auto at = *w.begin_resume(a, *w.add_route());
        w.set_cleanup_safe(a, false); w.close(a);
        check(!w.envs[a].attempt && w.resume_step(at) == 0 && w.receipts.begin()->second.disp.empty() && !w.envs[a].ended, "恢復中關閉：嘗試失效；收尾不安全時結果保留");
        w.set_cleanup_safe(a, true);
        check(w.receipts.begin()->second.disp == "不適用" && w.collect(a).empty() && w.envs[a].ended, "宣告安全：判定不適用、交付、範圍結束");
        inv(w, "S15 恢復中關閉");
    }
    {   // lost reporter notices keep working while dependants are paused; owner can declare from the receiver
        World w(16);
        auto s1 = w.add_service();
        auto A = *w.add_env("A", Contract::Handover, std::nullopt, {s1}), B = *w.add_env("B", Contract::Handover, std::nullopt, {s1});
        std::size_t got = 0;
        w.set_lost_receiver([&](const LostNotice& n) { ++got; w.declare_ended(n.handoff, n.kind); });
        auto h = w.fail_env(A, s1); auto d = *w.deliver(h);
        { ReportHandle x = std::move(d.lease); }
        d.callback.report(); d.work.report(); d.ack.report(); settle_all(w);
        check(got == 1 && w.handoffs.count(h) == 0 && w.envs[B].state == EnvState::Paused, "B 暫停期間，A 交接的遺失通知照送並可宣告結清");
        inv(w, "S15 遺失通知");
    }
    std::printf("\nfailures=%d pass=%d\n", g_fail, g_pass);
    return g_fail ? 1 : 0;
}
