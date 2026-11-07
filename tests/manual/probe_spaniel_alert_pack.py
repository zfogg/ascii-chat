"""Read-only query checks plus reversible active-session switching; sends no OTLP.
Run manually with Spaniel and the repository YAML definitions loaded.
"""
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment
import json
from pathlib import Path
import urllib.request
import yaml

ROOT = Path(__file__).resolve().parents[2]
BASE = "http://127.0.0.1:28080"
def api(path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(BASE + path, data=data, headers={"Content-Type":"application/json"})
    try:
        return json.load(urllib.request.urlopen(req, timeout=40))["data"]
    except urllib.error.HTTPError as e:
        raise AssertionError(e.read().decode()) from e

def draft(definition, query):
    c = definition["condition"]
    condition = {"kind":c["kind"]}
    if "threshold" in c: condition.update(operator=c["operator"], value=c["threshold"])
    return api("/api/alerts/preview", {"name":"Query-only verification", "query_sql":query,
        "condition":condition, "group_by":definition.get("group_by", []), "severity":"warning",
        "pending_for_ns":0, "cooldown_ns":0, "repeat_interval_ns":0,
        "browser_enabled":False, "pushover_enabled":False})

def main():
    dashboards = [yaml.safe_load(p.read_text(encoding="utf-8")) for p in (ROOT/"deploy/otel/dashboards").glob("*.yaml")]
    alerts = [yaml.safe_load(p.read_text(encoding="utf-8")) for p in (ROOT/"deploy/otel/alerts").glob("*.yaml")]
    for definition in alerts:
        assert "$session_id" in definition["query"], definition["id"]
        api("/api/alerts/"+definition["id"]+"/preview", {})
    for definition in dashboards:
        for panel in definition["panels"]:
            assert "$session_id" in panel["query"], panel["title"]
            api("/api/dashboards/"+definition["id"]+"/query-preview",
                {"query_sql":panel["query"], "display_type":panel["display_type"]})
    print(f"{len(alerts)} alert queries and {sum(len(d['panels']) for d in dashboards)} panels validated", flush=True)
    health = next(d for d in dashboards if d["name"].endswith("Session health"))
    def metric_fixture(panel_title, rows, age_ns=100000000000):
        panel = next(p for p in health["panels"] if p["title"].startswith(panel_title))
        selections = []
        for process, seconds, value, session in rows:
            selections.append(f"SELECT {session} AS session_id,'ascii-chat-client' AS service_name,"
                f"'ascii-chat.frames.captured' AS name,'sum' AS type,{process}::BIGINT AS start_timestamp_ns,"
                f"'series' AS series_key,epoch_ns(current_timestamp)-{age_ns}+{seconds}::BIGINT*1000000000 AS timestamp_ns,"
                f"{value}::DOUBLE AS value")
        sql = ("WITH spans AS (SELECT $session_id AS session_id,0::BIGINT AS end_ns), "
               "logs AS (SELECT $session_id AS session_id,0::BIGINT AS timestamp_ns), metrics AS ("
               + " UNION ALL ".join(selections) + "), " + panel["query"].removeprefix("WITH "))
        return api("/api/dashboards/"+health["id"]+"/query-preview", {"query_sql":sql,"display_type":panel["display_type"]})["rows"]
    # Independent producers contribute 1 + 2 frames/s, rather than averaging their rates.
    # A reset is omitted; the next increase uses its new baseline. First-only points
    # and telemetry from another recording session must not contribute.
    rows = metric_fixture("Frame stage throughput", [
        (1,10,0,"$session_id"),(1,20,10,"$session_id"),
        (1,30,5,"$session_id"),(1,40,15,"$session_id"),
        (2,10,0,"$session_id"),(2,20,20,"$session_id"),
        (3,20,500,"$session_id"),(4,10,0,"'foreign'"),(4,20,900,"'foreign'")])
    assert [row["value"] for row in rows] == [3,1], rows
    stale = metric_fixture("Frame stage throughput", [
        (1,10,0,"$session_id"),(1,20,10,"$session_id")],age_ns=86400000000000)
    assert len(stale)==1 and stale[0]["value"]==1,stale
    panel = next(p for p in health["panels"] if p["title"].startswith("Capture, conversion, mixing"))
    histogram = """WITH metrics AS (
      SELECT $session_id AS session_id,'ascii-chat-client' AS service_name,
      'ascii-chat.media.capture.duration' AS name,'histogram' AS type,p AS start_timestamp_ns,'series' AS series_key,
      floor(epoch_ns(current_timestamp)/10000000000)::BIGINT*10000000000+t AS timestamp_ns,
      n AS histogram_count,s AS histogram_sum
      FROM (VALUES (1,1,1,0.005),(1,2,2,0.020),(2,2,1,0.040)) AS v(p,t,n,s)
    ), """ + panel["query"].removeprefix("WITH ")
    result = api("/api/dashboards/"+health["id"]+"/query-preview", {"query_sql":histogram,"display_type":"time_series"})["rows"]
    assert len(result)==1 and abs(result[0]["value"]-20)<1e-8,result
    print("dashboard rates: process sums, resets, first samples, session isolation and weighted histograms passed",flush=True)
    for category in ("crypto", "protocol", "discovery"):
        definition = next(d for d in alerts if d["id"] == "ascii-chat-"+category+"-failures")
        for session, status, expected in [("$session_id",2,1),("'other-session'",2,0),("$session_id",0,0)]:
            fixture = f"WITH spans AS (SELECT {session} AS session_id,'ascii-chat-client' AS service_name,'fixture' AS trace_id,'ascii-chat.failure.{category}' AS name,{status} AS status_code,epoch_ns(current_timestamp) AS end_ns) "
            assert len(draft(definition,fixture+definition["query"])["rows"]) == expected
    reconnect = next(d for d in alerts if d["id"] == "ascii-chat-connection-failures")
    for count in (0,2,3):
        fixture = f"""WITH metrics AS (
          SELECT $session_id AS session_id,'ascii-chat-client' AS service_name,'ascii-chat.network.connection.attempts.failed' AS name,
          epoch_ns(current_timestamp)-100000000000 AS start_timestamp_ns,'series' AS series_key,
          epoch_ns(current_timestamp)-60000000000 AS timestamp_ns,0::DOUBLE AS value
          UNION ALL SELECT $session_id,'ascii-chat-client','ascii-chat.network.connection.attempts.failed',
          epoch_ns(current_timestamp)-100000000000,'series',epoch_ns(current_timestamp),{count}::DOUBLE
        ), """
        q=fixture+reconnect["query"].removeprefix("WITH ")
        assert draft(reconnect,q)["rows"][0]["value"] == count
    stalled = next(d for d in alerts if d["id"] == "ascii-chat-pipeline-stalled")
    for age in (0,6):
        fixture = f"""WITH metrics AS (SELECT $session_id AS session_id,'ascii-chat-mirror' AS service_name,
          'ascii-chat.pipeline.display.stall_age' AS name,1::BIGINT AS start_timestamp_ns,'series' AS series_key,
          epoch_ns(current_timestamp) AS timestamp_ns,{age}::DOUBLE AS value), """
        assert draft(stalled,fixture+stalled["query"].removeprefix("WITH "))["rows"][0]["value"] == age
    print("five alert queries: failure/recovery and foreign-session exclusion checks passed",flush=True)
    original=api("/api/sessions/active")["id"]
    # Existing empty session: no data is created, copied, or ingested.
    other="0e0cb367-ee3d-4509-9116-6ca6ae4b4b0e"
    probe="SELECT count(*) AS value FROM spans WHERE session_id=$session_id"
    observed=[]
    try:
        for selected in (original,other,original):
            api("/api/sessions/"+selected+"/activate",{})
            assert api("/api/sessions/active")["id"]==selected
            dashboard=api("/api/dashboards/"+dashboards[0]["id"]+"/query-preview",{"query_sql":probe,"display_type":"single_value","variables":{"session_id":"override-must-be-ignored"}})
            d=dict(alerts[0],condition={"kind":"threshold","operator":">=","threshold":1},group_by=[])
            alert=draft(d,probe)
            counts=(dashboard["rows"][0]["value"],alert["rows"][0]["value"])
            assert counts[0]==counts[1],counts
            observed.append(counts[0])
            for definition in alerts: api("/api/alerts/"+definition["id"]+"/preview",{})
            for definition in dashboards:
                for panel in definition["panels"]:
                    api("/api/dashboards/"+definition["id"]+"/query-preview",{"query_sql":panel["query"],"display_type":panel["display_type"]})
            print("active session",selected,"dashboard/alert scoped spans",counts[0],flush=True)
        assert observed[0]>0 and observed[1]==0 and observed[2]==observed[0],observed
    finally:
        api("/api/sessions/"+original+"/activate",{})
    print("active session restored; override rejected; all saved queries ran in both sessions",flush=True)

if __name__=="__main__":main()
