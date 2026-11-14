import { execFileSync, spawn } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";

const quotePowerShell = (value) => `'${String(value).replaceAll("'", "''")}'`;

const argumentForStartProcess = (value) => {
  const text = String(value);
  return /[\s\"]/.test(text) ? `\"${text.replaceAll('"', '\\"')}\"` : text;
};

export function launchNative(binary, args, { cwd, env, log }) {
  if (process.platform !== "win32") {
    const child = spawn(binary, args, {
      cwd,
      env,
      stdio: "ignore",
    });
    return {
      child,
      get exitCode() {
        return child.exitCode;
      },
      kill() {
        child.kill();
      },
      once(...values) {
        child.once(...values);
      },
      text() {
        return existsSync(log) ? readFileSync(log, "utf8") : "";
      },
    };
  }

  const environment = Object.entries(env ?? {})
    .filter(([key, value]) => process.env[key] !== value)
    .map(([key, value]) => `$env:${key} = ${quotePowerShell(value)}`)
    .join("; ");
  const clearAsciiChatEnvironment =
    "Get-ChildItem Env: | Where-Object Name -like 'ASCII_CHAT_*' | Remove-Item";
  const pidFile = `${log}.pid`;
  const powershell = [
    "$ErrorActionPreference = 'Stop'",
    clearAsciiChatEnvironment,
    environment,
    `$process = Start-Process -FilePath ${quotePowerShell(binary)} -ArgumentList @(${args
      .map(argumentForStartProcess)
      .map(quotePowerShell)
      .join(
        ",",
      )}) -WorkingDirectory ${quotePowerShell(cwd)} -WindowStyle Hidden -PassThru`,
    `Set-Content -LiteralPath ${quotePowerShell(pidFile)} -Value $process.Id`,
    "Wait-Process -Id $process.Id",
    "$process.Refresh()",
    "exit $process.ExitCode",
  ]
    .filter(Boolean)
    .join("; ");
  const child = spawn(
    "pwsh.exe",
    ["-NoLogo", "-NoProfile", "-NonInteractive", "-Command", powershell],
    { cwd, windowsHide: true, stdio: "ignore" },
  );
  return {
    child,
    get exitCode() {
      return child.exitCode;
    },
    kill() {
      const nativePid = existsSync(pidFile)
        ? Number(readFileSync(pidFile, "utf8"))
        : 0;
      if (nativePid) {
        try {
          execFileSync(
            "taskkill.exe",
            ["/PID", String(nativePid), "/T", "/F"],
            {
              stdio: "ignore",
            },
          );
        } catch {
          // The process may already have exited.
        }
      }
      child.kill();
    },
    once(...values) {
      child.once(...values);
    },
    text() {
      return existsSync(log) ? readFileSync(log, "utf8") : "";
    },
  };
}
