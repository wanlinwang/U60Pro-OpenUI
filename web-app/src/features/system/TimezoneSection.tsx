import { useEffect, useState } from 'react'
import { api } from '../../data/api'
import { parseSystemTimezone } from '../../data/timezone'
import { t } from '../../i18n'
import { Button, Select } from '../../ui/controls'
import { toast, toastError } from '../../ui/feedback'
import { Card } from '../../ui/primitives'

function label(minutes: number) {
  const sign = minutes < 0 ? '−' : '+'
  const n = Math.abs(minutes)
  return `UTC${sign}${String(Math.floor(n / 60)).padStart(2, '0')}:${String(n % 60).padStart(2, '0')}`
}

export default function TimezoneSection() {
  const [current, setCurrent] = useState<{ offset_minutes: number | null; timezone: string } | null>(null)
  const [selected, setSelected] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState(false)
  useEffect(() => {
    let live = true
    api.systemTimezone().then(parseSystemTimezone).then(value => {
      if (!live) return
      setCurrent(value)
      setSelected(value.offset_minutes === null ? '' : String(value.offset_minutes))
    }).catch(() => { if (live) setError(true) })
    return () => { live = false }
  }, [])
  async function apply() {
    if (!selected) return
    setBusy(true)
    try {
      const value = parseSystemTimezone(await api.setSystemTimezone(Number(selected)))
      if (value.offset_minutes !== Number(selected)) throw new Error(t('Timezone readback differs'))
      setCurrent(value); setSelected(String(value.offset_minutes)); toast(t('System time zone updated'))
    } catch (e) {
      toastError(e, t('Failed to update system time zone'))
      try { const value = parseSystemTimezone(await api.systemTimezone()); setCurrent(value); setSelected(value.offset_minutes === null ? '' : String(value.offset_minutes)) } catch { setError(true) }
    } finally { setBusy(false) }
  }
  return <Card title={t('System time zone')}>
    {error && !current ? <p className="text-[12px] text-warn">{t('System time zone unavailable')}</p> : <div className="space-y-3">
      <p className="text-[12px] text-ink2">{t('The touchscreen clock and SMS use this time zone. Fixed UTC offsets do not adjust for daylight saving time.')}</p>
      <Select aria-label={t('System time zone')} value={selected} disabled={busy || !current} onChange={e => setSelected(e.target.value)}>
        {!selected && <option value="">{current ? t('Current custom time zone: {zone}', { zone: current.timezone }) : t('Loading…')}</option>}
        {Array.from({ length: 105 }, (_, i) => i * 15 - 720).map(n => <option key={n} value={n}>{label(n)}</option>)}
      </Select>
      <Button onClick={() => void apply()} loading={busy} disabled={!current || !selected || Number(selected) === current.offset_minutes}>{t('Apply')}</Button>
    </div>}
  </Card>
}
