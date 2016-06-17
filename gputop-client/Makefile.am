if ENABLE_WEBUI
remotedir = $(datadir)/remote
nobase_dist_remote_DATA = \
	gputop.js
endif

if ENABLE_NODE_CLIENTS
install-data-local:
	npm install -g --prefix=$(prefix) --production --cache-min 999999999
endif
