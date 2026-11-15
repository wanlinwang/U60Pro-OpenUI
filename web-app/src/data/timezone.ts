export type SystemTimezone = { offset_minutes: number | null; timezone: string }

export function parseSystemTimezone(value: unknown): SystemTimezone {
  if (!value || typeof value !== 'object') throw new Error()
  const { offset_minutes: offset, timezone } = value as Record<string, unknown>
  if (typeof timezone !== 'string' || !timezone || timezone.length > 128 || /[\x00-\x1f\x7f]/.test(timezone)) throw new Error()
  if (offset !== null && (typeof offset !== 'number' || !Number.isInteger(offset) || offset < -720 || offset > 840 || offset % 15 !== 0)) throw new Error()
  return { offset_minutes: offset as number | null, timezone }
}
