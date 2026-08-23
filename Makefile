SO ?= so

.PHONY: build test sanitize gobuild gotest check clean

# Solod build: the nested so/ module resolves the real solod.dev.
build:
	$(SO) build -o config-init ./so

test:
	$(SO) test ./so

sanitize:
	$(SO) test -sanitize ./so

# The same sources built with the regular Go toolchain against gocompat/ —
# this is what pre-commit's golang language produces for hook consumers.
gobuild:
	go build -o config-init-go .

# Run the identical test suite against the gocompat implementations.
gotest:
	go -C so run -modfile=gotest.mod ./test

check: build gobuild test gotest

clean:
	rm -f config-init config-init-go
