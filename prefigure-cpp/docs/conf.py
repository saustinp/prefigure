# Sphinx configuration for PreFigure C++ documentation

project = 'PreFigure C++'
copyright = '2026, David Austin & contributors'
author = 'David Austin'
version = '0.6.0'
release = '0.6.0'

# Extensions
extensions = [
    'breathe',
    'sphinx.ext.mathjax',
    'sphinx.ext.todo',
    'sphinx.ext.githubpages',
]

# Breathe configuration — connects Sphinx to Doxygen XML output
breathe_projects = {
    'prefigure': '_build/doxygen/xml',
}
breathe_default_project = 'prefigure'
breathe_default_members = ('members', 'undoc-members')

# Theme
html_theme = 'sphinx_rtd_theme'
html_theme_options = {
    'navigation_depth': 4,
    'collapse_navigation': False,
    'sticky_navigation': True,
}

# General
templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']
html_static_path = ['_static']

# Don't show "View page source" links
html_show_sourcelink = False

# Math support
mathjax_path = 'https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js'
