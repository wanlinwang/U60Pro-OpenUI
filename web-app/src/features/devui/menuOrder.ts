import type { DevUIPageId } from '../../types'

/** Move a selected feature to another selected feature's position. */
export function moveMenuItem(items: DevUIPageId[], from: DevUIPageId, to: DevUIPageId): DevUIPageId[] {
  const start = items.indexOf(from), end = items.indexOf(to)
  if (start < 0 || end < 0 || start === end) return items
  const next = [...items]
  next.splice(start, 1)
  next.splice(end, 0, from)
  return next
}
