SO ?= so

.PHONY: build test sanitize generate clean

build:
	$(SO) build -o config-init .

test:
	$(SO) test ./cli

sanitize:
	$(SO) test -sanitize ./cli

# Regenerate the committed C translation used by the pre-commit script hook.
generate:
	rm -rf generated
	$(SO) translate -o generated .

clean:
	rm -rf config-init generated/.cache .cache
