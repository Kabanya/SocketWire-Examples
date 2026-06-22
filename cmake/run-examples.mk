# Run raylib examples in the same order as README.md.
#
# Usage:
#   make -f make/run-examples.make
#   make -f make/run-examples.make run-ship-swarm
#   make -f make/run-examples.make run-all CLIENTS=1
#   make -f make/run-examples.make run-all RUN_DURATION=10

SELF := $(abspath $(lastword $(MAKEFILE_LIST)))
MAKEFILE_DIR := $(abspath $(dir $(SELF)))
ROOT_DIR ?= $(abspath $(MAKEFILE_DIR)/..)

BUILD_DIR ?= $(ROOT_DIR)/build
BUILD_DIR_ABS := $(abspath $(BUILD_DIR))
BIN_DIR ?= $(BUILD_DIR_ABS)/bin
BIN_DIR_ABS := $(abspath $(BIN_DIR))

CMAKE ?= cmake
JOBS ?= auto

RUN_DELAY ?= 0.6
CLIENT_DELAY ?= 0.25
NEXT_DELAY ?= 0.4
RUN_DURATION ?= 0
CLIENTS ?= 2
CLIENT_HOST ?= 127.0.0.1

BENCH ?= 0
BENCH_DURATION_MS ?= 60000
BENCH_WARMUP_MS ?= 1000

ENTITY_EATER_PORT ?= 10131
PREDICTION_SHIPS_PORT ?= 10131
SHIP_SWARM_PORT ?= 10133
LOBBY_DOTS_LOBBY_PORT ?= 10887
LOBBY_DOTS_GAME_PORT ?= 10888
PROJECTILE_ARENA_PORT ?= 53477

ifeq ($(JOBS),auto)
PARALLEL_FLAG := --parallel
else
PARALLEL_FLAG := --parallel $(JOBS)
endif

RAYLIB_EXAMPLES := \
	entity-eater \
	lobby-dots \
	prediction-ships \
	ship-swarm \
	projectile-arena

RAYLIB_TARGETS := \
	entity-eater-server entity-eater-client \
	lobby-dots-lobby lobby-dots-game-server lobby-dots-client \
	prediction-ships-server prediction-ships-client \
	ship-swarm-server ship-swarm-client \
	projectile-arena-server projectile-arena-client

.DEFAULT_GOAL := run-all

.PHONY: help configure build run-all run-readme
.PHONY: run-entity-eater run-lobby-dots run-prediction-ships run-ship-swarm
.PHONY: run-projectile-arena
.PHONY: _start-entity-eater _start-lobby-dots _start-prediction-ships
.PHONY: _start-ship-swarm _start-projectile-arena
.PHONY: _run-server-clients _run-lobby-dots
.PHONY: $(addprefix build-,$(RAYLIB_TARGETS))

help:
	@printf '%s\n' 'Raylib examples runner'
	@printf '%s\n' ''
	@printf '%s\n' 'Default target: run-all'
	@printf '%s\n' ''
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  run-all | run-readme'
	@printf '%s\n' '  run-entity-eater | run-lobby-dots | run-prediction-ships'
	@printf '%s\n' '  run-ship-swarm | run-projectile-arena'
	@printf '%s\n' ''
	@printf '%s\n' 'Useful variables:'
	@printf '%s\n' '  CLIENTS=2          number of clients to start for each example'
	@printf '%s\n' '  RUN_DURATION=0     seconds before auto-stopping; 0 waits for clients'
	@printf '%s\n' '  CLIENT_HOST=127.0.0.1, ::1, localhost, or my-machine.local'
	@printf '%s\n' '  BENCH=1            run headless benchmark mode when supported'
	@printf '%s\n' '  BUILD_DIR=... BIN_DIR=... JOBS=...'

configure:
	$(CMAKE) -S "$(ROOT_DIR)" -B "$(BUILD_DIR_ABS)"

build: configure
	$(CMAKE) --build "$(BUILD_DIR_ABS)" $(PARALLEL_FLAG) --target $(RAYLIB_TARGETS)

$(addprefix build-,$(RAYLIB_TARGETS)): build-%: configure
	$(CMAKE) --build "$(BUILD_DIR_ABS)" $(PARALLEL_FLAG) --target $*

run-all run-readme: build
	@set -e; \
	for example in $(RAYLIB_EXAMPLES); do \
		$(MAKE) --no-print-directory -f "$(SELF)" _start-$$example; \
	done

run-entity-eater: build-entity-eater-server build-entity-eater-client
	@$(MAKE) --no-print-directory -f "$(SELF)" _start-entity-eater

_start-entity-eater:
	@$(MAKE) --no-print-directory -f "$(SELF)" _run-server-clients \
		EXAMPLE=entity-eater \
		SERVER=entity-eater-server \
		CLIENT=entity-eater-client \
		PORT="$(ENTITY_EATER_PORT)" \
		SERVER_ARGS="$(ENTITY_EATER_PORT)" \
		CLIENT_ARGS="$(ENTITY_EATER_PORT)"

run-prediction-ships: build-prediction-ships-server build-prediction-ships-client
	@$(MAKE) --no-print-directory -f "$(SELF)" _start-prediction-ships

_start-prediction-ships:
	@$(MAKE) --no-print-directory -f "$(SELF)" _run-server-clients \
		EXAMPLE=prediction-ships \
		SERVER=prediction-ships-server \
		CLIENT=prediction-ships-client \
		PORT="$(PREDICTION_SHIPS_PORT)" \
		SERVER_ARGS="$(PREDICTION_SHIPS_PORT)" \
		CLIENT_ARGS="$(PREDICTION_SHIPS_PORT)"

run-ship-swarm: build-ship-swarm-server build-ship-swarm-client
	@$(MAKE) --no-print-directory -f "$(SELF)" _start-ship-swarm

_start-ship-swarm:
	@$(MAKE) --no-print-directory -f "$(SELF)" _run-server-clients \
		EXAMPLE=ship-swarm \
		SERVER=ship-swarm-server \
		CLIENT=ship-swarm-client \
		PORT="$(SHIP_SWARM_PORT)" \
		SERVER_ARGS="$(SHIP_SWARM_PORT)" \
		CLIENT_ARGS="--host $(CLIENT_HOST) --port $(SHIP_SWARM_PORT)"

run-projectile-arena: build-projectile-arena-server build-projectile-arena-client
	@$(MAKE) --no-print-directory -f "$(SELF)" _start-projectile-arena

_start-projectile-arena:
	@$(MAKE) --no-print-directory -f "$(SELF)" _run-server-clients \
		EXAMPLE=projectile-arena \
		SERVER=projectile-arena-server \
		CLIENT=projectile-arena-client \
		PORT="$(PROJECTILE_ARENA_PORT)" \
		SERVER_ARGS="$(PROJECTILE_ARENA_PORT)" \
		CLIENT_ARGS="$(PROJECTILE_ARENA_PORT)"

run-lobby-dots: build-lobby-dots-lobby build-lobby-dots-game-server build-lobby-dots-client
	@$(MAKE) --no-print-directory -f "$(SELF)" _start-lobby-dots

_start-lobby-dots:
	@$(MAKE) --no-print-directory -f "$(SELF)" _run-lobby-dots

_run-server-clients:
	@set -eu; \
	case "$(CLIENTS)" in ''|*[!0-9]*|0) \
		printf '%s\n' 'CLIENTS must be a positive integer' >&2; exit 2;; \
	esac; \
	service_pids=""; \
	client_pids=""; \
	status=0; \
	bench_args=""; \
	if [ "$(BENCH)" = "1" ]; then \
		bench_args="--bench --port $(PORT) --duration-ms $(BENCH_DURATION_MS) --warmup-ms $(BENCH_WARMUP_MS) --clients $(CLIENTS)"; \
	fi; \
	start_service() { \
		printf '+ %s\n' "$$*"; \
		"$$@" & service_pids="$$service_pids $$!"; \
		sleep "$(RUN_DELAY)"; \
	}; \
	start_client() { \
		printf '+ %s\n' "$$*"; \
		"$$@" & client_pids="$$client_pids $$!"; \
		sleep "$(CLIENT_DELAY)"; \
	}; \
	cleanup() { \
		if [ -n "$$client_pids" ]; then \
			kill $$client_pids 2>/dev/null || true; \
			wait $$client_pids 2>/dev/null || true; \
		fi; \
		if [ -n "$$service_pids" ]; then \
			kill $$service_pids 2>/dev/null || true; \
			wait $$service_pids 2>/dev/null || true; \
		fi; \
	}; \
	trap cleanup EXIT; \
	trap 'cleanup; exit 130' INT TERM; \
	printf '\n== %s ==\n' "$(EXAMPLE)"; \
	start_service "$(BIN_DIR_ABS)/$(SERVER)" $(SERVER_ARGS) $$bench_args; \
	i=1; \
	while [ "$$i" -le "$(CLIENTS)" ]; do \
		run_arg=""; \
		if [ "$(BENCH)" = "1" ]; then run_arg="--run $$i"; fi; \
		start_client "$(BIN_DIR_ABS)/$(CLIENT)" $(CLIENT_ARGS) $$bench_args $$run_arg; \
		i=$$((i + 1)); \
	done; \
	if [ "$(RUN_DURATION)" = "0" ]; then \
		printf '%s\n' 'Close all client windows to continue to the next example.'; \
		for pid in $$client_pids; do wait "$$pid" || status=$$?; done; \
	else \
		sleep "$(RUN_DURATION)"; \
	fi; \
	cleanup; \
	trap - EXIT INT TERM; \
	sleep "$(NEXT_DELAY)"; \
	exit "$$status"

_run-lobby-dots:
	@set -eu; \
	case "$(CLIENTS)" in ''|*[!0-9]*|0) \
		printf '%s\n' 'CLIENTS must be a positive integer' >&2; exit 2;; \
	esac; \
	service_pids=""; \
	client_pids=""; \
	status=0; \
	bench_args=""; \
	if [ "$(BENCH)" = "1" ]; then \
		bench_args="--bench --duration-ms $(BENCH_DURATION_MS) --warmup-ms $(BENCH_WARMUP_MS) --clients $(CLIENTS)"; \
	fi; \
	start_service() { \
		printf '+ %s\n' "$$*"; \
		"$$@" & service_pids="$$service_pids $$!"; \
		sleep "$(RUN_DELAY)"; \
	}; \
	start_client() { \
		printf '+ %s\n' "$$*"; \
		"$$@" & client_pids="$$client_pids $$!"; \
		sleep "$(CLIENT_DELAY)"; \
	}; \
	cleanup() { \
		if [ -n "$$client_pids" ]; then \
			kill $$client_pids 2>/dev/null || true; \
			wait $$client_pids 2>/dev/null || true; \
		fi; \
		if [ -n "$$service_pids" ]; then \
			kill $$service_pids 2>/dev/null || true; \
			wait $$service_pids 2>/dev/null || true; \
		fi; \
	}; \
	trap cleanup EXIT; \
	trap 'cleanup; exit 130' INT TERM; \
	printf '\n== %s ==\n' 'lobby-dots'; \
	if [ "$(BENCH)" = "1" ]; then \
		start_service "$(BIN_DIR_ABS)/lobby-dots-lobby" --host "$(CLIENT_HOST)" --game-port "$(LOBBY_DOTS_GAME_PORT)" --lobby-port "$(LOBBY_DOTS_LOBBY_PORT)" $$bench_args; \
		start_service "$(BIN_DIR_ABS)/lobby-dots-game-server" --game-port "$(LOBBY_DOTS_GAME_PORT)" $$bench_args; \
	else \
		start_service "$(BIN_DIR_ABS)/lobby-dots-lobby" "$(CLIENT_HOST)" "$(LOBBY_DOTS_GAME_PORT)" "$(LOBBY_DOTS_LOBBY_PORT)"; \
		start_service "$(BIN_DIR_ABS)/lobby-dots-game-server" "$(LOBBY_DOTS_GAME_PORT)"; \
	fi; \
	i=1; \
	while [ "$$i" -le "$(CLIENTS)" ]; do \
		run_arg=""; \
		if [ "$(BENCH)" = "1" ]; then run_arg="--run $$i"; fi; \
		if [ "$(BENCH)" = "1" ]; then \
			start_client "$(BIN_DIR_ABS)/lobby-dots-client" --host "$(CLIENT_HOST)" --lobby-port "$(LOBBY_DOTS_LOBBY_PORT)" $$bench_args $$run_arg; \
		else \
			start_client "$(BIN_DIR_ABS)/lobby-dots-client" "$(LOBBY_DOTS_LOBBY_PORT)"; \
		fi; \
		i=$$((i + 1)); \
	done; \
	if [ "$(RUN_DURATION)" = "0" ]; then \
		printf '%s\n' 'Close all client windows to continue to the next example.'; \
		for pid in $$client_pids; do wait "$$pid" || status=$$?; done; \
	else \
		sleep "$(RUN_DURATION)"; \
	fi; \
	cleanup; \
	trap - EXIT INT TERM; \
	sleep "$(NEXT_DELAY)"; \
	exit "$$status"
