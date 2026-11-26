// A minimal Sankey layout for a fixed number of columns. Nodes are stacked top to bottom in their
// column, sized by the larger of what flows in and what flows out, on one watts-per-pixel scale
// shared by every column so that ribbon widths compare across the whole diagram. Links are drawn as
// filled ribbons rather than thick strokes, so a ribbon's two ends are exactly the slices it takes
// out of its source and target.
//
// d3-sankey would do this and more, but it brings d3-array and d3-shape with it into a bundle that
// is embedded in the firmware and ships in every OTA. Three columns don't need any of that.

export type SankeyNode = {
  id: string;
  column: number;
  // Overrides the layout's minNodeHeight for this node, e.g. to give a placeholder room for a label.
  minHeight?: number;
};

export type SankeyLink = {
  source: string;
  target: string;
  value: number;
};

export type SankeyOptions = {
  width: number;
  height: number;
  nodeWidth: number;
  // Vertical space between nodes in a column.
  gap: number;
  // Space left free on each side for labels; the first and last columns sit on these margins.
  marginLeft: number;
  marginRight: number;
  // A node carrying nothing still gets this many pixels, so it stays visible.
  minNodeHeight: number;
};

export type PlacedNode<N extends SankeyNode> = N & {
  x: number;
  y: number;
  height: number;
  value: number;
};

export type PlacedLink<L extends SankeyLink> = L & {
  path: string;
  // Pixel thickness of the ribbon.
  thickness: number;
};

export type SankeyLayout<N extends SankeyNode, L extends SankeyLink> = {
  nodes: PlacedNode<N>[];
  links: PlacedLink<L>[];
};

export function layoutSankey<N extends SankeyNode, L extends SankeyLink>(
  nodes: N[],
  links: L[],
  options: SankeyOptions,
): SankeyLayout<N, L> {
  const { width, height, nodeWidth, gap, marginLeft, marginRight, minNodeHeight } = options;

  const usable = links.filter((l) => l.value > 0);

  const valueOf = new Map<string, number>();

  for (const node of nodes) {
    const inflow = usable.filter((l) => l.target === node.id).reduce((s, l) => s + l.value, 0);
    const outflow = usable.filter((l) => l.source === node.id).reduce((s, l) => s + l.value, 0);
    valueOf.set(node.id, Math.max(inflow, outflow));
  }

  const columnCount = nodes.reduce((m, n) => Math.max(m, n.column + 1), 0);
  const columns: N[][] = Array.from({ length: columnCount }, () => []);

  for (const node of nodes) {
    columns[node.column].push(node);
  }

  // The scale is set by the busiest column, after its gaps and the minimum heights of its empty
  // nodes are taken out, so that every column fits.
  //
  let scale = Infinity;

  for (const column of columns) {
    if (column.length === 0) {
      continue;
    }

    const total = column.reduce((s, n) => s + (valueOf.get(n.id) ?? 0), 0);
    const reserved = column
      .filter((n) => (valueOf.get(n.id) ?? 0) === 0)
      .reduce((s, n) => s + (n.minHeight ?? minNodeHeight), 0);
    const free = height - gap * (column.length - 1) - reserved;

    if (total > 0 && free > 0) {
      scale = Math.min(scale, free / total);
    }
  }

  if (!Number.isFinite(scale)) {
    scale = 0;
  }

  const columnX = (c: number) => columnCount <= 1
    ? marginLeft
    : marginLeft + (c * (width - marginLeft - marginRight - nodeWidth)) / (columnCount - 1);

  const placed = new Map<string, PlacedNode<N>>();

  columns.forEach((column, c) => {
    const heights = column.map((n) => Math.max((valueOf.get(n.id) ?? 0) * scale, n.minHeight ?? minNodeHeight));
    const used = heights.reduce((s, h) => s + h, 0) + gap * Math.max(column.length - 1, 0);

    // A column shorter than the tallest is centred, so the ribbons fan out evenly.
    let y = Math.max((height - used) / 2, 0);

    column.forEach((node, i) => {
      placed.set(node.id, { ...node, x: columnX(c), y, height: heights[i], value: valueOf.get(node.id) ?? 0 });
      y += heights[i] + gap;
    });
  });

  // Each node hands out slices of its edge in the order of the nodes at the other end, top to
  // bottom, so ribbons leaving one node never cross each other.
  //
  const sourceOffset = new Map<string, number>();
  const targetOffset = new Map<string, number>();

  const centre = (id: string) => {
    const n = placed.get(id)!;
    return n.y + n.height / 2;
  };

  const bySource = [...usable].sort((a, b) => centre(a.target) - centre(b.target));
  const byTarget = [...usable].sort((a, b) => centre(a.source) - centre(b.source));

  const sourceY = new Map<L, number>();
  const targetY = new Map<L, number>();

  for (const link of bySource) {
    const offset = sourceOffset.get(link.source) ?? 0;
    sourceY.set(link, placed.get(link.source)!.y + offset);
    sourceOffset.set(link.source, offset + link.value * scale);
  }

  for (const link of byTarget) {
    const offset = targetOffset.get(link.target) ?? 0;
    targetY.set(link, placed.get(link.target)!.y + offset);
    targetOffset.set(link.target, offset + link.value * scale);
  }

  const placedLinks = usable
    .filter((l) => placed.has(l.source) && placed.has(l.target))
    .map((link) => {
      const thickness = link.value * scale;
      const x0 = placed.get(link.source)!.x + nodeWidth;
      const x1 = placed.get(link.target)!.x;
      const y0 = sourceY.get(link)!;
      const y1 = targetY.get(link)!;
      const xm = (x0 + x1) / 2;

      const path = [
        `M${x0},${y0}`,
        `C${xm},${y0} ${xm},${y1} ${x1},${y1}`,
        `L${x1},${y1 + thickness}`,
        `C${xm},${y1 + thickness} ${xm},${y0 + thickness} ${x0},${y0 + thickness}`,
        "Z",
      ].join(" ");

      return { ...link, path, thickness };
    });

  return { nodes: [...placed.values()], links: placedLinks };
}
