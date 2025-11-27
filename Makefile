SETUP := ./scripts/setup_env.sh

.PHONY: deps build image run all clean

deps:
	$(SETUP) deps

build:
	$(SETUP) build

image:
	$(SETUP) image

run:
	$(SETUP) run

all:
	$(SETUP) all

clean:
	$(SETUP) clean
