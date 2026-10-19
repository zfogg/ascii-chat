import { test, expect } from "@playwright/test";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

let directory: string;
let serverKey: string,
  clientKey: string,
  serverPublic: string,
  clientPublic: string;
test.beforeAll(() => {
  directory = mkdtempSync(join(tmpdir(), "acip-peer-test-"));
  for (const name of ["server", "client"]) {
    execFileSync("ssh-keygen", [
      "-t",
      "ed25519",
      "-N",
      "",
      "-f",
      join(directory, name),
    ]);
  }
  serverKey = readFileSync(join(directory, "server"), "utf8");
  clientKey = readFileSync(join(directory, "client"), "utf8");
  serverPublic = readFileSync(join(directory, "server.pub"), "utf8");
  clientPublic = readFileSync(join(directory, "client.pub"), "utf8");
});
test.afterAll(() => {
  if (directory) rmSync(directory, { recursive: true });
});

type Result = {
  ok: boolean;
  encrypted?: boolean[];
  prematureRejected?: boolean;
  received?: number[];
  error?: string;
};
for (const scenario of [
  "default",
  "explicit",
  "wrong-plaintext",
  "password",
  "password-auth-only",
  "password-client-no-encrypt",
  "keys-auth-only",
  "wrong-password",
  "wrong-server-key",
  "wrong-client-key",
]) {
  test(`WebRTC ${scenario}`, async ({ page }) => {
    await page.goto("/tests/network/peer-security.html");
    const password = { password: "peer-test-password" };
    const keys = {
      server: {
        identityPrivateKeyText: serverKey,
        allowedClientPublicKeyText: clientPublic,
      },
      client: {
        identityPrivateKeyText: clientKey,
        expectedServerPublicKeyText: serverPublic,
      },
    };
    const settings =
      scenario === "default"
        ? {}
        : scenario === "wrong-plaintext"
          ? { encryption: true, plaintextInjection: true }
          : scenario === "explicit"
            ? { encryption: true }
            : scenario === "password"
              ? { server: password, client: password }
              : scenario === "password-auth-only"
                ? { encryption: false, server: password, client: password }
                : scenario === "password-client-no-encrypt"
                  ? {
                      clientEncryption: false,
                      server: password,
                      client: password,
                    }
                  : scenario === "wrong-password"
                    ? {
                        server: password,
                        client: { password: "incorrect-password" },
                      }
                    : scenario === "wrong-server-key"
                      ? {
                          ...keys,
                          client: {
                            ...keys.client,
                            expectedServerPublicKeyText: clientPublic,
                          },
                        }
                      : scenario === "wrong-client-key"
                        ? {
                            ...keys,
                            server: {
                              ...keys.server,
                              allowedClientPublicKeyText: serverPublic,
                            },
                          }
                        : { ...keys, encryption: false };
    const result = await page.evaluate(async (settings) => {
      const runner = window as unknown as {
        runPeerSecurityTest(settings: unknown): Promise<Result>;
      };
      return runner.runPeerSecurityTest(settings);
    }, settings);
    if (scenario.startsWith("wrong-")) {
      expect(result.ok, JSON.stringify(result)).toBe(false);
      expect(result.received).toEqual([0, 0]);
      expect(result.error).not.toContain("timed out");
    } else {
      expect(result.ok, JSON.stringify(result)).toBe(true);
      expect(result.prematureRejected).toBe(true);
      const encrypted = scenario === "explicit" || scenario === "password";
      expect(result.encrypted).toEqual([encrypted, encrypted]);
    }
  });
}
