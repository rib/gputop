install-data-local:
	if ! [ -z $$http_proxy ]; then \
	npm --proxy $$http_proxy install -g --prefix=$(prefix) --production --cache-min 999999999; \
	else \
	npm install -g --prefix=$(prefix) --production --cache-min 999999999; \
	fi

clean-local:
	rm -fr node_modules

EXTRA_DIST=gputop-*.js
