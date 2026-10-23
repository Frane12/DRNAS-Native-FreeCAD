#!/usr/bin/env python3
"""Reject duplicate YAML keys and invalid shell steps before expensive builds."""
from pathlib import Path
import subprocess
import yaml

class UniqueLoader(yaml.BaseLoader):
    pass

def mapping(loader, node):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node)
        if key in result:
            raise ValueError(f'Duplicate YAML key: {key} (line {key_node.start_mark.line+1})')
        result[key] = loader.construct_object(value_node)
    return result

UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, mapping)
root = Path(__file__).resolve().parent.parent
workflow = yaml.load((root/'.github/workflows/android-bindings-sequential-pipeline.yml').read_text(), Loader=UniqueLoader)
assert 'workflow_dispatch' in workflow['on']
for job in workflow['jobs'].values():
    for step in job['steps']:
        if 'run' in step:
            subprocess.run(['bash', '-n'], input=step['run'], text=True, check=True)
subprocess.run(['bash', '-n', str(root/'full-port/build-bindings-stage.sh')], check=True)
print('PASS: unique workflow keys and valid Bash in every build step')
