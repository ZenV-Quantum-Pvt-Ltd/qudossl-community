#!/usr/bin/env python3
"""
Auto-generate C header constants from YAML metadata
Automates constant generation to keep code and documentation synchronized.

Usage: python generate_constants_from_yaml.py
"""

import yaml
import sys
import os

def load_yaml(filename):
    with open(filename, 'r') as f:
        return yaml.safe_load(f)

def generate_algorithm_constants(meta_file, output_file):
    """
    Read META.yml and auto-generate C header file
    
    WHY? Single source of truth - change YAML once, regenerate everything!
    """
    
    # Load YAML metadata
    metadata = load_yaml(meta_file)
    
    # Start generating C header
    output = []
    output.append("/**")
    output.append(" * Auto-generated from META.yml")
    output.append(" * DO NOT EDIT MANUALLY - Run generate_constants_from_yaml.py")
    output.append(" */")
    output.append("")
    output.append("#ifndef MLKEM_AUTO_GENERATED_H")
    output.append("#define MLKEM_AUTO_GENERATED_H")
    output.append("")
    
    # Generate constants for each algorithm
    for alg in metadata.get('algorithms', []):
        name = alg['name'].upper().replace('-', '_')
        output.append(f"/* {alg['name']} - {alg['description']} */")
        output.append(f"#define {name}_PUBLIC_KEY_BYTES    {alg['public_key_size']}")
        output.append(f"#define {name}_SECRET_KEY_BYTES    {alg['secret_key_size']}")
        output.append(f"#define {name}_CIPHERTEXT_BYTES    {alg['ciphertext_size']}")
        output.append(f"#define {name}_SHARED_SECRET_BYTES {alg['shared_secret_size']}")
        output.append(f"#define {name}_NIST_LEVEL           {alg['nist_level']}")
        output.append("")
    
    output.append("#endif /* MLKEM_AUTO_GENERATED_H */")
    
    # Write to file
    with open(output_file, 'w') as f:
        f.write('\n'.join(output))
    
    print(f"✓ Generated {output_file} from {meta_file}")
    print(f"✓ Generated {len(metadata.get('algorithms', []))} algorithms")

def generate_algorithm_list(metadata):
    """Generate list of algorithm names"""
    output = []
    output.append("/* Auto-generated algorithm list */")
    output.append("const char *mlkem_algorithms[] = {")
    
    for alg in metadata.get('algorithms', []):
        output.append(f'    "{alg["name"]}",')
    
    output.append("    NULL")
    output.append("};")
    return '\n'.join(output)

def generate_switch_case(metadata):
    """Generate switch case for algorithm selection"""
    output = []
    output.append("/* Auto-generated switch cases */")
    output.append("switch (algorithm) {")
    
    for i, alg in enumerate(metadata.get('algorithms', [])):
        name = alg['name'].upper().replace('-', '_')
        output.append(f"    case MLKEM_{alg['name'].split('-')[-1]}:")
        output.append(f"        *pk_len = {name}_PUBLIC_KEY_BYTES;")
        output.append(f"        *sk_len = {name}_SECRET_KEY_BYTES;")
        output.append(f"        *ct_len = {name}_CIPHERTEXT_BYTES;")
        output.append(f"        break;")
    
    output.append("    default:")
    output.append("        return -1;")
    output.append("}")
    return '\n'.join(output)

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    meta_file = os.path.join(script_dir, '..', 'META.yml')
    output_file = os.path.join(script_dir, '..', 'include', 'mlkem_auto_generated.h')
    
    if not os.path.exists(meta_file):
        print(f"ERROR: {meta_file} not found!")
        sys.exit(1)
    
    # Generate header file
    generate_algorithm_constants(meta_file, output_file)
