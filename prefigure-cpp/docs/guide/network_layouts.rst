Network Graph Layouts
=====================

The ``network.cpp`` module renders graph/network diagrams with nodes and
edges.  When node positions are not fully specified, a layout algorithm
computes them.  This guide describes each algorithm and when to use it.

Layout Algorithm Selection
--------------------------

The ``@layout`` attribute on a ``<network>`` element selects the algorithm:

.. list-table::
   :header-rows: 1
   :widths: 15 40 45

   * - Layout
     - When to use
     - Key properties
   * - ``spring`` (default)
     - General-purpose; unknown graph structure
     - Force-directed, non-deterministic (use ``@seed``)
   * - ``circular``
     - Emphasize symmetry; small graphs
     - Deterministic, all nodes on a circle
   * - ``random``
     - Baseline comparison; stress testing
     - Non-deterministic (use ``@seed``)
   * - ``bfs``
     - Hierarchical / tree-like graphs
     - Requires ``@start`` root node
   * - ``spectral``
     - Graphs with cluster structure
     - Uses Laplacian eigenvectors
   * - ``bipartite``
     - Two-group graphs
     - Requires ``@bipartite-set``
   * - ``planar``
     - Planar graphs
     - Falls back to spring if not planar

Spring Layout (Fruchterman-Reingold)
-------------------------------------

The default layout uses a force-directed algorithm matching networkx's
``spring_layout``:

1. **Initialization**: Nodes start at random positions (seeded by ``@seed``).
2. **Repulsive forces**: Every node pair repels with force proportional to
   ``k^2 / distance``, where ``k = 1 / sqrt(n_nodes)``.
3. **Attractive forces**: Connected nodes attract with force proportional to
   ``distance^2 / k``.
4. **Temperature cooling**: Displacements are capped by a "temperature" that
   decreases linearly over iterations.  Default: 50 iterations.
5. **Normalization**: Final positions are centered and scaled to [0, 1].

Use ``@seed`` for reproducible layouts.  Increase ``@iterations`` (passed as
a future extension) for dense graphs that need more settling time.

Spectral Layout
----------------

The spectral layout uses the Laplacian matrix's eigenvectors to position
nodes:

1. **Adjacency matrix**: Built from the symmetric adjacency list.  For
   undirected graphs, each edge appears in both directions.
2. **Degree matrix**: Diagonal matrix of node degrees.
3. **Laplacian**: ``L = D - A``.
4. **Eigendecomposition**: Compute all eigenvalues and eigenvectors of L
   using Eigen's ``SelfAdjointEigenSolver``.
5. **Position selection**: Use eigenvectors corresponding to the 2nd and
   3rd smallest eigenvalues (indices 1 and 2, skipping index 0 which is
   the trivial constant eigenvector).

**Why indices 1 and 2 (not 0 and 1)?**

The smallest eigenvalue of a graph Laplacian is always 0, with a constant
eigenvector.  The second-smallest eigenvalue (Fiedler value) and its
eigenvector capture the most significant structural partition of the graph.
The third captures the next level of structure.  Together they give the
best 2D embedding that preserves graph connectivity.

BFS Tree Layout
----------------

Arranges nodes in a hierarchical tree structure:

1. **BFS traversal**: Starting from ``@start``, visit nodes level by level.
2. **Level assignment**: Each node gets a y-coordinate based on its BFS level.
3. **Horizontal spreading**: Nodes within each level are evenly spaced
   across the width.

Best for trees and DAGs.  For graphs with cycles, the first BFS encounter
determines the level.

Bipartite Layout
-----------------

For graphs with two natural groups:

1. ``@bipartite-set`` specifies the handles in the first partition.
2. Nodes in set 1 are placed in one column; remaining nodes in the other.
3. Within each column, nodes are evenly spaced vertically.
4. ``@alignment`` controls orientation: "horizontal" (columns side by side)
   or "vertical" (rows stacked).

Edge Rendering
--------------

**Single edges**: Straight ``<line>`` elements between node centers.

**Multi-edges**: When two nodes share multiple edges, quadratic Bezier
curves are used.  Each edge is offset perpendicular to the line between
nodes, with spacing proportional to edge index.

**Self-loops**: When a node has an edge to itself, a cubic Bezier circle
is drawn.  The loop direction is chosen by finding the angular gap with
the largest open space (away from other connected nodes).  The ``@loop-scale``
attribute controls the loop radius.

**Directed edges**: For ``@directed="yes"``, arrowheads are placed at the
edge endpoint.  A binary subdivision algorithm finds the exact point where
the edge meets the node boundary (approximated by the node marker radius).

Adjacency Dictionary Parsing
------------------------------

The ``@graph`` attribute accepts a Python-style dictionary string::

    graph="{'A': ['B', 'C'], 'B': ['C'], 'C': []}"

The ``parse_dict_string()`` function handles this format:

1. Strip outer braces
2. Split on ``:`` at depth 0 (respecting nested brackets)
3. Parse keys as quoted strings
4. Parse values as bracket-delimited lists of quoted strings
5. Build the symmetric ``AdjacencyList`` (each edge added in both directions
   for undirected graphs)
