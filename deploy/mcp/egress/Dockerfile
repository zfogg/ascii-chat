FROM alpine:3.22
RUN apk add --no-cache iptables
COPY egress-guard /usr/local/bin/egress-guard
RUN chmod 755 /usr/local/bin/egress-guard
ENTRYPOINT ["/usr/local/bin/egress-guard"]
