FROM zephyrprojectrtos/ci:v0.26.6

ARG NCLT_BASE="https://nsscprodmedia.blob.core.windows.net/prod/software-and-other-downloads/desktop-software/nrf-command-line-tools/sw/versions-10-x-x"
ARG NORDIC_COMMAND_LINE_TOOLS_VERSION="10-24-0/nrf-command-line-tools-10.24.0"

RUN mkdir tmpworkdir && cd tmpworkdir && \
	NCLT_URL="${NCLT_BASE}/${NORDIC_COMMAND_LINE_TOOLS_VERSION}_linux-amd64.tar.gz" && \
	wget -qO - "${NCLT_URL}" | tar --no-same-owner -xz && \
	# Install included JLink && \
	mkdir /opt/SEGGER && \
	tar xzf JLink_*.tgz -C /opt/SEGGER && \
	mv /opt/SEGGER/JLink* /opt/SEGGER/JLink && \
	# Install nrf-command-line-tools && \
	cp -r ./nrf-command-line-tools /opt && \
	ln -s /opt/nrf-command-line-tools/bin/nrfjprog /usr/local/bin/nrfjprog && \
	ln -s /opt/nrf-command-line-tools/bin/mergehex /usr/local/bin/mergehex && \
	cd .. && rm -rf tmpworkdir ;
