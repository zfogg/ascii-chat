"""Validate and install the bundled dashboards through Spaniel MCP.

Run: python deploy/otel/apply_dashboards.py [--dry-run] [--endpoint URL]
Existing dashboards with matching names are replaced rather than duplicated.
"""
import argparse
import json
from pathlib import Path
import re
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--endpoint', default='http://127.0.0.1:28080/mcp')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}

    def rpc(method, params):
        request = urllib.request.Request(args.endpoint, json.dumps({
            'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': params,
        }).encode(), headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            session = response.headers.get('Mcp-Session-Id')
            if session:
                headers['Mcp-Session-Id'] = session
            body = response.read().decode('utf-8')
        if body.startswith('event:'):
            body = next(line[6:] for line in body.splitlines() if line.startswith('data: '))
        result = json.loads(body)
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result['result']

    def call(name, **arguments):
        result = rpc('tools/call', {'name': name, 'arguments': arguments})
        if result.get('isError'):
            raise RuntimeError(result)
        return '\n'.join(block.get('text', '') for block in result.get('content', []))

    rpc('initialize', {'protocolVersion': '2025-03-26', 'capabilities': {},
                       'clientInfo': {'name': 'ascii-chat-dashboards', 'version': '1'}})
    info = call('get_server_info')
    if not args.dry_run and 'writes_enabled: true' not in info:
        parser.error('Spaniel MCP writes are disabled; start it with --mcp-allow-writes')
    listing = call('list_dashboards', search='ascii-chat', limit=500)
    existing = {name: identifier for identifier, name in re.findall(
        r'^\s*([0-9a-f-]{36}),([^,\n]+),', listing, re.MULTILINE)}
    origin = urllib.parse.urlsplit(args.endpoint)
    base = f'{origin.scheme}://{origin.netloc}'
    for path in sorted((Path(__file__).parent / 'dashboards').glob('*.json')):
        definition = json.loads(path.read_text(encoding='utf-8'))
        call('validate_dashboard', definition=definition)
        if args.dry_run:
            print(f'Validated: {definition["name"]}')
            continue
        arguments = {'definition': definition}
        if definition['name'] in existing:
            arguments['dashboard_id'] = existing[definition['name']]
        result = call('apply_dashboard', **arguments)
        match = re.search(r'link: (\S+)', result)
        if not match:
            raise RuntimeError(f'Spaniel did not return a dashboard link: {result}')
        print(f'{definition["name"]}: {base}{match[1]}')


if __name__ == '__main__':
    main()
