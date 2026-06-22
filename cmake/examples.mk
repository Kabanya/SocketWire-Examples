BUILD_DIR ?= build
BIN_DIR ?= $(BUILD_DIR)/bin
CMAKE ?= cmake
JOBS ?= auto
RUN_DELAY ?= 0.5
RUN_DURATION ?= 0
RUN_ARGS ?=

ECHO_PORT ?= 40404
MATH_DUEL_PORT ?= 2025
PACKET_STREAM_PORT ?= 53473
CHANNELS_DEMO_PORT ?= 53474
LARGE_MESSAGE_DEMO_PORT ?= 53475
STATS_WINDOW_DEMO_PORT ?= 53476
ENTITY_EATER_PORT ?= 10131
PREDICTION_SHIPS_PORT ?= 10132
SHIP_SWARM_PORT ?= 10133
LOBBY_DOTS_LOBBY_PORT ?= 10887
LOBBY_DOTS_GAME_PORT ?= 10888
PROJECTILE_ARENA_PORT ?= 53477
NETBENCH_PORT ?= 53490
NETBENCH_CLIENT_COUNTS ?= 1 100 1000 10000 100000 1000000
NETBENCH_PROFILE ?= flood
NETBENCH_DURATION_MS ?= 60000
NETBENCH_WARMUP_MS ?= 5000
NETBENCH_DRAIN_MS ?= 1000
NETBENCH_SERVER_EXTRA_MS ?= 60000
NETBENCH_SERVER_WORKERS ?= 1
NETBENCH_METRICS ?= $(BUILD_DIR)/netbench/socketwire-stress.jsonl

ifeq ($(JOBS),auto)
PARALLEL_FLAG := --parallel
else
PARALLEL_FLAG := --parallel $(JOBS)
endif

SIMPLE_TARGETS := \
	bitstream-demo constants-demo address-demo safe-bitstream-demo \
	echo-server echo-client \
	math-duel-server math-duel-client \
	packet-stream-server packet-stream-client \
	channels-demo-server channels-demo-client \
	large-message-demo-server large-message-demo-client \
	stats-window-demo-server stats-window-demo-client \
	crypto-handshake-demo thread-pool-demo

RAYLIB_TARGETS := \
	entity-eater-server entity-eater-client \
	lobby-dots-lobby lobby-dots-game-server lobby-dots-client \
	prediction-ships-server prediction-ships-client \
	ship-swarm-server ship-swarm-client \
	projectile-arena-server projectile-arena-client

NETWORK_BENCH_TARGETS := netbench-socketwire-server netbench-socketwire-client

EXAMPLE_TARGETS := $(SIMPLE_TARGETS) $(RAYLIB_TARGETS) $(NETWORK_BENCH_TARGETS)
BUILD_TARGET_ALIASES := $(addprefix build-,$(EXAMPLE_TARGETS)) build-SocketWireTests
RUN_TARGET_ALIASES := $(addprefix run-,$(EXAMPLE_TARGETS))

SIMPLE_RUN_SPECS := \
	bitstream-demo constants-demo address-demo safe-bitstream-demo crypto-handshake-demo thread-pool-demo \
	echo-server:$(ECHO_PORT) echo-client:$(ECHO_PORT) \
	math-duel-server:$(MATH_DUEL_PORT) math-duel-client:$(MATH_DUEL_PORT) \
	packet-stream-server:$(PACKET_STREAM_PORT) packet-stream-client:$(PACKET_STREAM_PORT) \
	channels-demo-server:$(CHANNELS_DEMO_PORT) channels-demo-client:$(CHANNELS_DEMO_PORT) \
	large-message-demo-server:$(LARGE_MESSAGE_DEMO_PORT) large-message-demo-client:$(LARGE_MESSAGE_DEMO_PORT) \
	stats-window-demo-server:$(STATS_WINDOW_DEMO_PORT) stats-window-demo-client:$(STATS_WINDOW_DEMO_PORT)

RAYLIB_RUN_SPECS := \
	entity-eater-server:$(ENTITY_EATER_PORT) entity-eater-client:$(ENTITY_EATER_PORT) \
	prediction-ships-server:$(PREDICTION_SHIPS_PORT) prediction-ships-client:$(PREDICTION_SHIPS_PORT) \
	ship-swarm-server:$(SHIP_SWARM_PORT) ship-swarm-client:$(SHIP_SWARM_PORT) \
	projectile-arena-server:$(PROJECTILE_ARENA_PORT) projectile-arena-client:$(PROJECTILE_ARENA_PORT) \
	lobby-dots-lobby:localhost:$(LOBBY_DOTS_GAME_PORT):$(LOBBY_DOTS_LOBBY_PORT) \
	lobby-dots-game-server:$(LOBBY_DOTS_GAME_PORT) \
	lobby-dots-client:$(LOBBY_DOTS_LOBBY_PORT)

.DEFAULT_GOAL := build

.PHONY: all help configure build build-all-targets build-examples build-simple-examples build-raylib-examples clean rebuild test
.PHONY: run-echo run-math-duel run-packet-stream run-channels-demo run-large-message-demo run-stats-window-demo
.PHONY: run-entity-eater run-prediction-ships run-ship-swarm run-projectile-arena run-lobby-dots
.PHONY: run-simple-examples run-raylib-examples run-all-examples
.PHONY: run-network-bench-sweep
.PHONY: _run-pair _run-lobby _run-group
.PHONY: $(BUILD_TARGET_ALIASES) $(RUN_TARGET_ALIASES)

all: build

help:
	@printf '%s\n' 'SocketWire Examples Make targets'
	@printf '%s\n' ''
	@printf '%s\n' 'Build:'
	@printf '%s\n' '  make configure | make build | make build-examples'
	@printf '%s\n' '  make build-simple-examples | make build-raylib-examples'
	@printf '%s\n' '  make build-<cmake-target>     example: make build-echo-server'
	@printf '%s\n' '  make clean | make rebuild | make test'
	@printf '%s\n' ''
	@printf '%s\n' 'Run single binaries:'
	@printf '%s\n' '  make run-<binary> RUN_ARGS="..."'
	@printf '%s\n' ''
	@printf '%s\n' 'Run combined examples:'
	@printf '%s\n' '  make run-echo | make run-math-duel | make run-packet-stream'
	@printf '%s\n' '  make run-channels-demo | make run-large-message-demo | make run-stats-window-demo'
	@printf '%s\n' '  make run-entity-eater | make run-prediction-ships | make run-ship-swarm'
	@printf '%s\n' '  make run-projectile-arena | make run-lobby-dots'
	@printf '%s\n' '  make run-network-bench-sweep'
	@printf '%s\n' ''
	@printf '%s\n' 'Run groups:'
	@printf '%s\n' '  make run-simple-examples'
	@printf '%s\n' '  make run-raylib-examples'
	@printf '%s\n' '  make run-all-examples RUN_DURATION=3'

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)"

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" $(PARALLEL_FLAG)

build-all-targets: build-examples

build-examples: build-simple-examples build-raylib-examples

build-simple-examples: configure
	$(CMAKE) --build "$(BUILD_DIR)" $(PARALLEL_FLAG) --target $(SIMPLE_TARGETS)

build-raylib-examples: configure
	$(CMAKE) --build "$(BUILD_DIR)" $(PARALLEL_FLAG) --target $(RAYLIB_TARGETS)

$(BUILD_TARGET_ALIASES): build-%: configure
	$(CMAKE) --build "$(BUILD_DIR)" $(PARALLEL_FLAG) --target $*

build-%: configure
	$(CMAKE) --build "$(BUILD_DIR)" $(PARALLEL_FLAG) --target $*

clean:
	@if [ -d "$(BUILD_DIR)" ]; then \
		$(CMAKE) --build "$(BUILD_DIR)" --target clean; \
	else \
		printf '%s\n' 'Nothing to clean'; \
	fi

rebuild: clean build

test: configure
	$(CMAKE) --build "$(BUILD_DIR)" $(PARALLEL_FLAG) --target SocketWireTests
	"$(BIN_DIR)/SocketWireTests"

$(RUN_TARGET_ALIASES): run-%: build-%
	"$(BIN_DIR)/$*" $(RUN_ARGS)

run-%: build-%
	"$(BIN_DIR)/$*" $(RUN_ARGS)

define pair_target
run-$(1): build-$(2) build-$(3)
	@$$(MAKE) --no-print-directory _run-pair RUN_SERVER=$(2) RUN_CLIENT=$(3) RUN_PORT=$$($(4))
endef

$(eval $(call pair_target,echo,echo-server,echo-client,ECHO_PORT))
$(eval $(call pair_target,math-duel,math-duel-server,math-duel-client,MATH_DUEL_PORT))
$(eval $(call pair_target,packet-stream,packet-stream-server,packet-stream-client,PACKET_STREAM_PORT))
$(eval $(call pair_target,channels-demo,channels-demo-server,channels-demo-client,CHANNELS_DEMO_PORT))
$(eval $(call pair_target,large-message-demo,large-message-demo-server,large-message-demo-client,LARGE_MESSAGE_DEMO_PORT))
$(eval $(call pair_target,stats-window-demo,stats-window-demo-server,stats-window-demo-client,STATS_WINDOW_DEMO_PORT))
$(eval $(call pair_target,entity-eater,entity-eater-server,entity-eater-client,ENTITY_EATER_PORT))
$(eval $(call pair_target,prediction-ships,prediction-ships-server,prediction-ships-client,PREDICTION_SHIPS_PORT))
$(eval $(call pair_target,ship-swarm,ship-swarm-server,ship-swarm-client,SHIP_SWARM_PORT))
$(eval $(call pair_target,projectile-arena,projectile-arena-server,projectile-arena-client,PROJECTILE_ARENA_PORT))

run-lobby-dots: build-lobby-dots-lobby build-lobby-dots-game-server build-lobby-dots-client
	@$(MAKE) --no-print-directory _run-lobby

run-simple-examples: build-simple-examples
	@$(MAKE) --no-print-directory _run-group RUN_SPECS='$(SIMPLE_RUN_SPECS)'

run-raylib-examples: build-raylib-examples
	@$(MAKE) --no-print-directory _run-group RUN_SPECS='$(RAYLIB_RUN_SPECS)'

run-all-examples: build-examples
	@$(MAKE) --no-print-directory _run-group RUN_SPECS='$(SIMPLE_RUN_SPECS) $(RAYLIB_RUN_SPECS)'

run-network-bench-sweep: build-netbench-socketwire-server build-netbench-socketwire-client
	@set +e; \
	mkdir -p "$$(dirname "$(NETBENCH_METRICS)")"; \
	: > "$(NETBENCH_METRICS)"; \
	run=0; \
	for clients in $(NETBENCH_CLIENT_COUNTS); do \
		echo "+ network-bench clients=$$clients profile=$(NETBENCH_PROFILE)"; \
		server_duration=$$(( $(NETBENCH_WARMUP_MS) + $(NETBENCH_DURATION_MS) + $(NETBENCH_DRAIN_MS) + $(NETBENCH_SERVER_EXTRA_MS) )); \
		"$(BIN_DIR)/netbench-socketwire-server" \
			--port "$(NETBENCH_PORT)" \
			--clients "$$clients" \
			--profile "$(NETBENCH_PROFILE)" \
			--duration-ms "$$server_duration" \
			--warmup-ms 0 \
			--drain-ms 0 \
			--server-workers "$(NETBENCH_SERVER_WORKERS)" \
			--metrics "$(NETBENCH_METRICS)" \
			--run "$$run" & \
		server_pid="$$!"; \
		sleep "$(RUN_DELAY)"; \
		"$(BIN_DIR)/netbench-socketwire-client" \
			--host 127.0.0.1 \
			--port "$(NETBENCH_PORT)" \
			--clients "$$clients" \
			--profile "$(NETBENCH_PROFILE)" \
			--duration-ms "$(NETBENCH_DURATION_MS)" \
			--warmup-ms "$(NETBENCH_WARMUP_MS)" \
			--drain-ms "$(NETBENCH_DRAIN_MS)" \
			--metrics "$(NETBENCH_METRICS)" \
			--run "$$run"; \
		client_status="$$?"; \
		wait "$$server_pid"; \
		server_status="$$?"; \
		if [ "$$client_status" != "0" ] || [ "$$server_status" != "0" ]; then \
			echo "network-bench clients=$$clients client_status=$$client_status server_status=$$server_status"; \
		fi; \
		run=$$((run + 1)); \
	done

_run-pair:
	@set -e; \
	pids=""; \
	start_bg() { echo "+ $$*"; "$$@" & pids="$$pids $$!"; sleep "$(RUN_DELAY)"; }; \
	cleanup() { if [ -n "$$pids" ]; then kill $$pids 2>/dev/null || true; wait $$pids 2>/dev/null || true; fi; }; \
	trap cleanup EXIT; \
	trap 'cleanup; exit 130' INT TERM; \
	start_bg "$(BIN_DIR)/$(RUN_SERVER)" "$(RUN_PORT)"; \
	if [ "$(RUN_DURATION)" = "0" ]; then \
		echo "+ $(BIN_DIR)/$(RUN_CLIENT) $(RUN_PORT)"; \
		"$(BIN_DIR)/$(RUN_CLIENT)" "$(RUN_PORT)"; \
	else \
		start_bg "$(BIN_DIR)/$(RUN_CLIENT)" "$(RUN_PORT)"; \
		sleep "$(RUN_DURATION)"; \
	fi

_run-lobby:
	@set -e; \
	pids=""; \
	start_bg() { echo "+ $$*"; "$$@" & pids="$$pids $$!"; sleep "$(RUN_DELAY)"; }; \
	cleanup() { if [ -n "$$pids" ]; then kill $$pids 2>/dev/null || true; wait $$pids 2>/dev/null || true; fi; }; \
	trap cleanup EXIT; \
	trap 'cleanup; exit 130' INT TERM; \
	start_bg "$(BIN_DIR)/lobby-dots-lobby" localhost "$(LOBBY_DOTS_GAME_PORT)" "$(LOBBY_DOTS_LOBBY_PORT)"; \
	start_bg "$(BIN_DIR)/lobby-dots-game-server" "$(LOBBY_DOTS_GAME_PORT)"; \
	if [ "$(RUN_DURATION)" = "0" ]; then \
		echo "+ $(BIN_DIR)/lobby-dots-client $(LOBBY_DOTS_LOBBY_PORT)"; \
		"$(BIN_DIR)/lobby-dots-client" "$(LOBBY_DOTS_LOBBY_PORT)"; \
	else \
		start_bg "$(BIN_DIR)/lobby-dots-client" "$(LOBBY_DOTS_LOBBY_PORT)"; \
		sleep "$(RUN_DURATION)"; \
	fi

_run-group:
	@set -e; \
	pids=""; \
	start() { echo "+ $$*"; "$$@" </dev/null & pids="$$pids $$!"; sleep "$(RUN_DELAY)"; }; \
	start_spec() { \
		spec="$$1"; old_ifs="$$IFS"; IFS=':'; set -- $$spec; IFS="$$old_ifs"; \
		exe="$$1"; shift; start "$(BIN_DIR)/$$exe" "$$@"; \
	}; \
	cleanup() { if [ -n "$$pids" ]; then kill $$pids 2>/dev/null || true; wait $$pids 2>/dev/null || true; fi; }; \
	trap cleanup EXIT; \
	trap 'cleanup; exit 130' INT TERM; \
	for spec in $(RUN_SPECS); do start_spec "$$spec"; done; \
	if [ "$(RUN_DURATION)" = "0" ]; then \
		echo 'Processes running. Press Ctrl+C to stop.'; \
		wait; \
	else \
		sleep "$(RUN_DURATION)"; \
	fi
