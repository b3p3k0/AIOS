SETUP := ./scripts/setup_env.sh

.PHONY: deps hello image run all clean

deps:
	$(SETUP) deps

hello:
	$(SETUP) build

image:
	$(SETUP) image

run:
	$(SETUP) run

all:
	$(SETUP) all

clean:
	$(SETUP) clean
