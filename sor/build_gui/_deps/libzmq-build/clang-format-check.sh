#!/bin/sh
FAILED=0
IFS=";"
FILES="../../../tests/integration/test_order_lifecycle.cpp;../../../tests/integration/test_partial_fills.cpp;../../../tests/integration/test_venue_failover.cpp;../../../tests/test_helpers.h;../../../tests/test_main_listener.cpp;../../../tests/unit/test_alpaca_normalizer.cpp;../../../tests/unit/test_feed_quality_monitor.cpp;../../../tests/unit/test_fixed_point.cpp;../../../tests/unit/test_market_data.cpp;../../../tests/unit/test_memory_pool.cpp;../../../tests/unit/test_order_state_machine.cpp;../../../tests/unit/test_risk_manager.cpp;../../../tests/unit/test_routing_strategies.cpp;../../../tests/unit/test_sor_controller.cpp;../../../tests/unit/test_thread_safe_aggregator.cpp"
IDS=$(echo -en "\n\b")
for FILE in $FILES
do
	clang-format -style=file -output-replacements-xml "$FILE" | grep "<replacement " >/dev/null &&
    {
      echo "$FILE is not correctly formatted"
	  FAILED=1
	}
done
if [ "$FAILED" -eq "1" ] ; then exit 1 ; fi
