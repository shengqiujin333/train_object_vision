// railhmi-core 有界丢最旧队列测试（视频 ITEM-008 纯逻辑）。
// 显示（容量 1）/推理（有界丢旧）/录像（条数+字节上限）三策略与计数。

#include "test_util.h"

#include <string>

#include "railhmi/core/bounded_queue.h"

namespace {

using namespace railhmi::core;

void test_latest_only_capacity_one() {
    BoundedQueue<int> q(1);
    q.push(1);
    q.push(2);
    q.push(3);
    railhmi_test::check(q.size() == 1, "cap1 size=1");
    int v = 0;
    railhmi_test::check(q.latest(&v) && v == 3, "cap1 latest=3");
    railhmi_test::check(q.dropped() == 2, "cap1 dropped=2");
    railhmi_test::check(q.pushed() == 3, "cap1 pushed=3");
    railhmi_test::check(q.pop(&v) && v == 3, "cap1 pop=3");
    railhmi_test::check(!q.pop(&v), "cap1 empty pop=false");
}

void test_drop_oldest_bounded() {
    BoundedQueue<std::string> q(2);
    q.push("a");
    q.push("b");
    q.push("c");
    railhmi_test::check(q.size() == 2, "cap2 size=2");
    railhmi_test::check(q.dropped() == 1, "cap2 dropped=1");
    std::string s;
    railhmi_test::check(q.pop(&s) && s == "b", "cap2 FIFO first=b");
    railhmi_test::check(q.pop(&s) && s == "c", "cap2 FIFO second=c");
    railhmi_test::check(q.popped() == 2, "cap2 popped=2");
}

void test_byte_budget() {
    BoundedQueue<int> q(1000, 100);  // 条数宽松、字节上限 100
    q.push(1, 40);
    q.push(2, 40);
    railhmi_test::check(q.size() == 2 && q.bytes() == 80, "byte budget 80");
    q.push(3, 40);  // 120 > 100 -> 丢最旧
    railhmi_test::check(q.size() == 2 && q.bytes() == 80, "byte budget evicts to 80");
    railhmi_test::check(q.dropped() == 1, "byte budget dropped=1");
    int v = 0;
    railhmi_test::check(q.pop(&v) && v == 2, "byte budget first=2");
}

void test_count_and_byte_both_bounded() {
    BoundedQueue<int> q(2, 1000);
    q.push(1, 10);
    q.push(2, 10);
    q.push(3, 10);
    railhmi_test::check(q.size() == 2 && q.pushed() == 3 && q.dropped() == 1, "count+byte bounded");
}

void test_empty_semantics() {
    BoundedQueue<int> q(4);
    int v = 0;
    railhmi_test::check(!q.pop(&v) && !q.latest(&v) && q.empty(), "empty pop/latest/empty");
    railhmi_test::check(q.dropped() == 0 && q.pushed() == 0, "empty counters zero");
}

}  // namespace

void run_bounded_queue_tests() {
    test_latest_only_capacity_one();
    test_drop_oldest_bounded();
    test_byte_budget();
    test_count_and_byte_both_bounded();
    test_empty_semantics();
}
