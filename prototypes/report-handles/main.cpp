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

int main() {
    std::printf("== S1 型別：只能移動、不能複製、不能由公開資料拼出 (Q140/Q142) ==\n");
    check(!std::is_copy_constructible_v<ReportHandle> && !std::is_copy_assignable_v<ReportHandle>, "不可複製");
    check(std::is_nothrow_move_constructible_v<ReportHandle> && std::is_nothrow_move_assignable_v<ReportHandle>, "可移動且不拋例外");
    check(!std::is_constructible_v<ReportHandle, std::weak_ptr<Ledger>, Ledger::Ref>, "外部無法由 ledger＋編號建構把手");
    { ReportHandle empty; check(empty.report({1}) == Reject::Empty, "預設把手回報 → 空把手（比命令先到的結果無從送出）"); }

    std::printf("== S2 預留不足就不承擔 (Q135) ==\n");
    {
        World w(3); auto a = *w.add_env("A", Contract::Handover);
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
        World w(16); auto a = *w.add_env("A", Contract::Handover);
        auto h = w.fault(a);
        auto d1 = *w.deliver(h);
        check(w.reassign(h), "改派：撤銷舊確認資格");
        check(d1.ack.report() == Reject::Revoked && w.ledger().rejected(Reject::Revoked) == 1, "舊接管者確認 → 已撤權，並記診斷");
        std::thread([cb = std::move(d1.callback), wk = std::move(d1.work)]() mutable { cb.report(); wk.report(); }).join();
        { ReportHandle lostLease = std::move(d1.lease); }  // lease reporter lost
        settle_all(w);
        auto& x = w.handoffs.at(h);
        check(!x.cb.active && !x.work.active && x.lease.active && x.lease.lost && w.ownerNotices.size() == 1, "租用把手遺失：不推定已歸還，標記並通知擁有者");
        auto d2 = *w.deliver(h);
        check(!d2.lease.valid(), "舊租用未結清：新指派不另發租用把手");
        d2.callback.report(); d2.work.report(); d2.ack.report();
        settle_all(w);
        check(w.handoffs.count(h) == 1, "已確認但遺失的租用未處置 → 交接不釋放");
        w.set_cleanup_safe(a, false);
        check(!w.resolve_lost(h), "收尾不安全：擁有者不能處置遺失");
        w.set_cleanup_safe(a, true);
        check(w.resolve_lost(h) && w.handoffs.count(h) == 0, "宣告安全後擁有者處置 → 交接釋放");
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
        World w(16); auto a = *w.add_env("A", Contract::Handover);
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
            World w(16); auto a = *w.add_env("A", Contract::Handover);
            auto h = w.fault(a);
            auto d = *w.deliver(h);
            { ReportHandle c = std::move(d.callback), wk = std::move(d.work); }
            g_failAfter = k;
            try { w.settle(); } catch (const std::bad_alloc&) { ++threw; }
            g_failAfter = -1;
            settle_all(w);
            auto& x = w.handoffs.at(h);
            const bool ok = x.cb.lost && x.work.lost && x.cb.active && x.work.active && w.ownerNotices.size() == 2 && w.invariants().empty();
            if (!ok) { allOk = false; std::printf("  F8 k=%d cbLost=%d workLost=%d notices=%zu inv=%zu\n", k, x.cb.lost, x.work.lost, w.ownerNotices.size(), w.invariants().size()); }
        }
        check(allOk && threw > 0, "F8：結算途中配置失敗，回報仍留在帳本，下次結算不遺失");
    }
#else
    std::printf("  SKIP F7/F8 配置失敗注入需要替換 operator new\n");
#endif

    std::printf("\nfailures=%d pass=%d\n", g_fail, g_pass);
    return g_fail ? 1 : 0;
}
