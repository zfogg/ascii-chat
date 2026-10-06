import { useEffect, useState } from "react";
import { Heading } from "@ascii-chat/shared/components";

type Server = {
  host: string;
  port: number;
  up: boolean;
};

type CheckResult = {
  up: boolean;
  servers: Server[];
};

type Service = {
  label: string;
  endpoint: string;
  protocol: "webrtc" | "stun" | "turn";
  numbered?: boolean;
};

const SERVICES: Service[] = [
  { label: "WebRTC", endpoint: "/api/status/webrtc", protocol: "webrtc" },
  {
    label: "STUN",
    endpoint: "/api/status/stun",
    protocol: "stun",
    numbered: true,
  },
  { label: "TURN", endpoint: "/api/status/turn", protocol: "turn" },
];

const formatServer = (
  protocol: Service["protocol"],
  { host, port }: Server,
) => {
  if (protocol === "webrtc")
    return `${port === 80 ? "ws" : "wss"}://${host}:${port}`;
  return `${protocol}:${host}:${port}`;
};

export default function ServerStatusSection() {
  const [checks, setChecks] = useState<Record<string, CheckResult>>({});

  useEffect(() => {
    const controller = new AbortController();

    const load = async () => {
      const results = await Promise.all(
        SERVICES.map(async ({ endpoint }) => {
          const response = await fetch(endpoint, { signal: controller.signal });
          if (!response.ok)
            throw new Error(`Status request failed: ${endpoint}`);
          return [endpoint, (await response.json()) as CheckResult] as const;
        }),
      );
      setChecks(Object.fromEntries(results));
    };

    void load().catch(() => {
      if (!controller.signal.aborted) setChecks({});
    });

    return () => controller.abort();
  }, []);

  return (
    <section className="mb-12" aria-labelledby="server-status-heading">
      <Heading
        id="server-status-heading"
        level={2}
        className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
      >
        📡 Server status
      </Heading>
      <p className="leading-relaxed mb-4 text-base md:text-lg text-gray-300">
        Live reachability from the site’s Vercel function.
      </p>
      <div className="overflow-x-auto rounded-lg border border-gray-700">
        <table className="w-full text-left text-sm md:text-base">
          <thead className="bg-gray-800 text-gray-200">
            <tr>
              <th scope="col" className="px-4 py-3 font-semibold">
                Server type
              </th>
              <th scope="col" className="px-4 py-3 font-semibold">
                Status
              </th>
              <th scope="col" className="px-4 py-3 font-semibold">
                Endpoints checked
              </th>
            </tr>
          </thead>
          <tbody className="divide-y divide-gray-700">
            {SERVICES.flatMap(({ label, endpoint, protocol, numbered }) => {
              const check = checks[endpoint];
              const servers = check?.servers || [];
              if (servers.length === 0) {
                return (
                  <tr key={endpoint}>
                    <td className="px-4 py-3 font-medium text-gray-100">
                      {label}
                    </td>
                    <td className="px-4 py-3 text-gray-400">… Checking</td>
                    <td className="px-4 py-3 font-mono text-xs text-gray-300">
                      Checking…
                    </td>
                  </tr>
                );
              }

              return servers.map((server, index) => {
                const isUp = server.up;
                return (
                  <tr key={`${endpoint}-${server.host}-${server.port}`}>
                    <td className="px-4 py-3 font-medium text-gray-100">
                      {numbered ? `${label} (${index + 1})` : label}
                    </td>
                    <td className="px-4 py-3 whitespace-nowrap">
                      <span
                        aria-label={isUp ? "Up" : "Down"}
                        className={isUp ? "text-green-400" : "text-red-400"}
                      >
                        {isUp ? "✅ Up" : "❌ Down"}
                      </span>
                    </td>
                    <td className="px-4 py-3 font-mono text-xs text-gray-300">
                      {formatServer(protocol, server)}
                    </td>
                  </tr>
                );
              });
            })}
          </tbody>
        </table>
      </div>
      <p className="mt-2 text-xs text-gray-400">
        STUN servers are used in the priority shown (1, then 2) and passed to
        ascii-chat as a comma-separated list.
      </p>
    </section>
  );
}
