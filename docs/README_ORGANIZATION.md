# README Organization Guide

This document describes the organization of README files throughout the repository.

## Repository Structure

```
blocks-o-code-v3/
├── README.md                    # Main project README
├── docs/
│   ├── README.md               # Documentation index
│   └── [various docs]
├── firmware_blocks/
│   ├── README.md               # Firmware blocks overview
│   ├── FRAMEWORK.md            # Firmware framework documentation
│   ├── brain_block/
│   │   └── README.md           # Brain Block specific docs
│   ├── child_block_1/
│   │   └── README.md           # Child Block 1 docs
│   ├── child_block_2/
│   │   └── README.md           # Child Block 2 docs
│   └── block_templates/
│       └── [template READMEs]  # Template documentation
├── companion_app/
│   └── README.md               # Companion app documentation
└── scripts/
    └── [script docs]
```

## README Files by Location

### Root Level

- **`README.md`**: Main project README
  - Project overview
  - Quick start guide
  - Repository structure
  - Links to detailed documentation

### Documentation (`docs/`)

- **`docs/README.md`**: Documentation index
  - Links to all documentation sections
  - Documentation status tracking
  - Quick navigation

### Firmware Blocks (`firmware_blocks/`)

- **`firmware_blocks/README.md`**: Firmware overview
  - Introduction to firmware blocks
  - Building and flashing instructions
  - Common tasks

- **`firmware_blocks/FRAMEWORK.md`**: Framework documentation
  - Block contract definitions
  - Per-block requirements
  - Development guidelines

- **`firmware_blocks/brain_block/README.md`**: Brain Block docs
  - Brain Block specific features
  - Configuration details
  - Usage examples

- **`firmware_blocks/child_block_*/README.md`**: Child block docs
  - Block-specific functionality
  - Hardware specifications
  - Usage examples

### Companion App (`companion_app/`)

- **`companion_app/README.md`**: App documentation
  - App features
  - Setup instructions
  - Development guide

## README Guidelines

### Content Standards

1. **Clear Purpose**: Each README should clearly state its purpose
2. **Getting Started**: Include quick start instructions
3. **Structure**: Document the directory structure
4. **Links**: Link to related documentation
5. **Examples**: Include code examples where helpful

### Formatting

- Use Markdown formatting consistently
- Include code blocks with syntax highlighting
- Use clear headings and subheadings
- Include tables for structured data
- Add diagrams/images where helpful

### Maintenance

- Keep READMEs up-to-date with code changes
- Review READMEs during code reviews
- Update when adding new features
- Remove obsolete information

## Updating READMEs

When making changes:

1. **Update relevant READMEs** for affected components
2. **Check cross-references** to ensure links still work
3. **Update examples** if APIs change
4. **Review for clarity** and completeness

## README Checklist

For each README, ensure it includes:

- [ ] Clear title and description
- [ ] Purpose/overview section
- [ ] Getting started guide
- [ ] Directory structure (if applicable)
- [ ] Key features/capabilities
- [ ] Usage examples
- [ ] Links to related docs
- [ ] Troubleshooting section (if applicable)
