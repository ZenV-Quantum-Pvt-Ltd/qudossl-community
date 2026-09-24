#!/usr/bin/env python3
"""
Generate documentation from YAML metadata
Keeps documentation synchronized with code automatically.
"""

import yaml
import os

def load_yaml(filename):
    with open(filename, 'r') as f:
        return yaml.safe_load(f)

def generate_algorithm_table(metadata):
    """Generate markdown table of algorithms"""
    
    output = []
    output.append("# ML-KEM Algorithms")
    output.append("")
    output.append("| Algorithm | NIST Level | Public Key | Secret Key | Ciphertext | Security |")
    output.append("|-----------|------------|------------|------------|------------|----------|")
    
    for alg in metadata.get('algorithms', []):
        output.append(
            f"| {alg['name']} | "
            f"{alg['nist_level']} | "
            f"{alg['public_key_size']} | "
            f"{alg['secret_key_size']} | "
            f"{alg['ciphertext_size']} | "
            f"{alg['claimed_security']} |"
        )
    
    output.append("")
    output.append("## Platform Support")
    output.append("")
    
    for platform in metadata.get('platforms', []):
        output.append(f"### {platform['name']}")
        output.append(f"- Architectures: {', '.join(platform['architectures'])}")
        output.append(f"- Tested: {'✓' if platform.get('tested') else '✗'}")
        output.append("")
    
    return '\n'.join(output)

def generate_readme_section(metadata):
    """Generate README section"""
    
    output = []
    output.append("## Supported Algorithms")
    output.append("")
    
    for alg in metadata.get('algorithms', []):
        output.append(f"### {alg['name']}")
        output.append(f"- **Type:** {alg['type']}")
        output.append(f"- **NIST Level:** {alg['nist_level']}")
        output.append(f"- **Security:** {alg['claimed_security']}-bit")
        output.append(f"- **Description:** {alg['description']}")
        output.append("")
    
    return '\n'.join(output)

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    meta_file = os.path.join(script_dir, '..', 'META.yml')
    doc_file = os.path.join(script_dir, '..', 'docs', 'ALGORITHMS.md')
    
    metadata = load_yaml(meta_file)
    
    # Generate documentation
    os.makedirs(os.path.join(script_dir, '..', 'docs'), exist_ok=True)
    
    with open(doc_file, 'w') as f:
        f.write(generate_algorithm_table(metadata))
        f.write("\n\n")
        f.write(generate_readme_section(metadata))
    
    print(f"✓ Generated {doc_file}")
    print("\n=== AUTOMATION BENEFITS ===")
    print("✓ Documentation always matches YAML")
    print("✓ Add new algorithm? Just update YAML + re-run script")
    print("✓ No manual copy-paste errors")
    print("✓ CI/CD can verify docs are up-to-date")
