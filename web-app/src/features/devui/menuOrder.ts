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

/** Transfer one item between the candidate and display areas, or reorder it. */
export function placeMenuItem(items: DevUIPageId[], item: DevUIPageId, target: 'display' | 'candidate', index: number, limit: number): DevUIPageId[] {
  const start = items.indexOf(item)
  if (target === 'candidate') return start < 0 ? items : items.filter(id => id !== item)
  if (!Number.isInteger(index) || index < 0 || (start < 0 && items.length >= limit)) return items
  const end = Math.min(index, start < 0 ? items.length : items.length - 1)
  if (start === end) return items
  const next = [...items]
  if (start >= 0) next.splice(start, 1)
  next.splice(end, 0, item)
  return next
}
