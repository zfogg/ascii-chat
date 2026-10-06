import { generateSessionStrings } from "../../packages/shared/src/utils/sessionStrings.js";

const MAX_COUNT = 20;

function parseCount(request: Request): number | undefined {
  const count = new URL(request.url).searchParams.get("count") ?? "1";

  if (!/^\d+$/.test(count)) {
    return undefined;
  }

  const parsed = Number(count);
  return parsed >= 1 && parsed <= MAX_COUNT ? parsed : undefined;
}

export function GET(request: Request): Response {
  const count = parseCount(request);

  if (count === undefined) {
    return Response.json(
      { error: `count must be an integer between 1 and ${MAX_COUNT}` },
      { status: 400 },
    );
  }

  const strings = generateSessionStrings(count);
  return Response.json(
    { count: strings.length, strings },
    {
      headers: {
        "Cache-Control": "no-store",
      },
    },
  );
}

export default { fetch: GET };
