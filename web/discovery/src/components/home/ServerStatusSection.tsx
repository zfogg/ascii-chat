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
};

const SERVICES: Service[] = [
  { label: "WebRTC", endpoint: "/api/status/webrtc", protocol: "webrtc" },
  { label: "STUN", endpoint: "/api/status/stun", protocol: "stun" },
  { label: "coturn / TURN", endpoint: "/api/status/turn", protocol: "turn" },
];

const formatServers = (
  protocol: Service["protocol"],
  servers: Server[] | undefined,
) =>
  servers
    ?.map(({ host, port }) => {
      if (protocol === "webrtc")
        return `${port === 80 ? "ws" : "wss"}://${host}:${port}`;
      return `${protocol}:${host}:${port}`;
    })
    .join(", ") || "Checking…";

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
            {SERVICES.map(({ label, endpoint, protocol }) => {
              const check = checks[endpoint];
              const isUp = check?.up === true;
              const isKnownDown = check !== undefined && !isUp;
              return (
                <tr key={endpoint}>
                  <td className="px-4 py-3">
                    <div className="font-medium text-gray-100">{label}</div>
                  </td>
                  <td className="px-4 py-3 whitespace-nowrap">
                    <span
                      aria-label={
                        isUp ? "Up" : isKnownDown ? "Down" : "Checking"
                      }
                      className={
                        isUp
                          ? "text-green-400"
                          : isKnownDown
                            ? "text-red-400"
                            : "text-gray-400"
                      }
                    >
                      {isUp ? "✅ Up" : isKnownDown ? "❌ Down" : "… Checking"}
                    </span>
                  </td>
                  <td className="px-4 py-3 font-mono text-xs text-gray-300">
                    {formatServers(protocol, check?.servers)}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </section>
  );
}
