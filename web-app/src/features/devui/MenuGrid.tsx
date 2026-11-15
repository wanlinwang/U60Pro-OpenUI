import { useRef, useState } from 'react'
import type { KeyboardEvent, PointerEvent } from 'react'
import { getLang, t } from '../../i18n'
import type { DevUIPageId } from '../../types'
import { placeMenuItem } from './menuOrder'

export type MenuFeature = { id: DevUIPageId; title: string; description: string }
const CAPACITY = 18
type Zone = 'display' | 'candidate'
type Drop = { zone: Zone; index: number }
type Drag = { id: DevUIPageId; startX: number; startY: number; moved: boolean }

// Match the shorter labels used on the device's 96px-wide tiles.
// i18n-ignore: these are the touchscreen renderer's bilingual tile labels
const shortTitles: Partial<Record<DevUIPageId, [string, string]>> = {
  usb: ['USB & TTL', 'USB/TTL'], power: ['Battery', '电池'], booster: ['Booster', '加速'], // i18n-ignore
  profiles: ['Profiles', '配置'], devices: ['Routing', '分流'], // i18n-ignore
}

export default function MenuGrid({ pages, items, disabled, onChange }: {
  pages: MenuFeature[]; items: DevUIPageId[]; disabled: boolean; onChange: (items: DevUIPageId[]) => void
}) {
  const dragging = useRef<Drag | null>(null)
  const [target, setTarget] = useState<Drop | null>(null)
  const [ghost, setGhost] = useState<{ id: DevUIPageId; x: number; y: number } | null>(null)
  const displayed = items.map(id => pages.find(p => p.id === id)).filter((p): p is MenuFeature => !!p)
  const candidates = pages.filter(p => !items.includes(p.id))

  function place(id: DevUIPageId, zone: Zone, index: number) {
    if (disabled) return
    const next = placeMenuItem(items, id, zone, index, CAPACITY)
    if (next !== items) onChange(next)
  }
  function targetAt(x: number, y: number): Drop | null {
    const hit = document.elementFromPoint(x, y)?.closest<HTMLElement>('[data-menu-zone]')
    const zone = hit?.dataset.menuZone
    if (zone !== 'display' && zone !== 'candidate') return null
    return { zone, index: hit?.dataset.menuIndex === undefined ? items.length : Number(hit.dataset.menuIndex) }
  }
  function down(e: PointerEvent<HTMLButtonElement>, id: DevUIPageId) {
    if (disabled || e.button !== 0) return
    dragging.current = { id, startX: e.clientX, startY: e.clientY, moved: false }
    e.currentTarget.setPointerCapture(e.pointerId)
  }
  function move(e: PointerEvent<HTMLButtonElement>) {
    const drag = dragging.current
    if (!drag) return
    if (!drag.moved && Math.hypot(e.clientX - drag.startX, e.clientY - drag.startY) < 5) return
    drag.moved = true
    if (e.clientY < 48) window.scrollBy(0, -12)
    else if (e.clientY > window.innerHeight - 48) window.scrollBy(0, 12)
    setGhost({ id: drag.id, x: e.clientX, y: e.clientY })
    setTarget(targetAt(e.clientX, e.clientY))
  }
  function clearDrag() { dragging.current = null; setTarget(null); setGhost(null) }
  function up(e: PointerEvent<HTMLButtonElement>) {
    const drag = dragging.current
    const drop = drag?.moved ? targetAt(e.clientX, e.clientY) : null
    clearDrag()
    if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId)
    if (drag && drop) place(drag.id, drop.zone, drop.index)
  }
  function keyboard(e: KeyboardEvent<HTMLButtonElement>, id: DevUIPageId) {
    const index = items.indexOf(id)
    if (index < 0) return
    const delta = e.key === 'ArrowLeft' ? -1 : e.key === 'ArrowRight' ? 1 : e.key === 'ArrowUp' ? -3 : e.key === 'ArrowDown' ? 3 : 0
    if (!delta) return
    e.preventDefault()
    place(id, 'display', Math.max(0, Math.min(items.length - 1, index + delta)))
  }
  function tileTitle(feature: MenuFeature) {
    const short = shortTitles[feature.id]
    return short ? short[getLang() === 'zh' ? 1 : 0] : t(feature.title)
  }
  function cell(feature: MenuFeature | undefined, zone: Zone, index: number) {
    const highlighted = target?.zone === zone && (zone === 'candidate' || target.index === index)
    return <div key={feature?.id ?? `empty-${index}`} data-menu-zone={zone} data-menu-index={index}
      data-menu-feature={feature?.id} className={`relative h-[50px] min-w-0 rounded-lg ${highlighted ? 'ring-2 ring-[#4c9eeb]' : ''}`}>
      {feature ? <>
        <button type="button" disabled={disabled} title={t(feature.description)} aria-label={t('Drag {name}', { name: t(feature.title) })}
          onPointerDown={e => down(e, feature.id)} onPointerMove={move} onPointerUp={up} onPointerCancel={clearDrag} onLostPointerCapture={clearDrag}
          onKeyDown={e => keyboard(e, feature.id)}
          className={`h-full w-full touch-none rounded-lg border border-[#30343a] bg-[#202329] px-2 py-1 text-left text-[#e8eaee] enabled:cursor-grab enabled:active:cursor-grabbing focus-visible:outline focus-visible:outline-2 focus-visible:outline-[#4c9eeb] disabled:opacity-50 ${ghost?.id === feature.id ? 'opacity-40' : ''}`}>
          <span className="block truncate text-[14px] font-semibold leading-5">{tileTitle(feature)}</span>
          <span className="block truncate pr-5 text-[11px] leading-4 text-[#9aa1ac]">{feature.id === 'overview' ? 'CPU · ↓ · ↑' : '—'}</span>
        </button>
        <button type="button" disabled={disabled || (zone === 'candidate' && items.length >= CAPACITY)}
          aria-label={zone === 'display' ? t('Move {name} to candidates', { name: t(feature.title) }) : t('Add {name} to display', { name: t(feature.title) })}
          title={zone === 'display' ? t('Move to candidates') : t('Add to display')}
          onClick={() => place(feature.id, zone === 'display' ? 'candidate' : 'display', items.length)}
          className="absolute bottom-0 right-0 flex h-6 w-6 items-center justify-center rounded-lg text-[17px] font-semibold text-[#9aa1ac] hover:bg-[#303a49] hover:text-white focus-visible:outline focus-visible:outline-2 focus-visible:outline-[#4c9eeb] disabled:opacity-30">{zone === 'display' ? '−' : '+'}</button>
      </> : <div className="h-full rounded-lg border border-dashed border-[#29313d]" aria-hidden="true" />}
    </div>
  }
  return <div className="grid gap-5 lg:grid-cols-2">
    <section aria-label={t('Home screen preview')} className="lg:col-span-2">
      <div className="mb-2 text-[13px] font-semibold text-ink">{t('Home screen preview')}</div>
      <div className="max-w-[320px] rounded-2xl bg-[#0b0d11] p-3 text-[#e8eaee]">
        <div aria-hidden="true" className="mb-2 flex items-center justify-between text-xs font-semibold"><span>12:34</span><span>▂▄▆ 5G ▰</span></div>
        <div className="mb-2 rounded-lg border border-[#292d34] bg-[#191c21] px-3 py-2 text-xs text-[#9aa1ac]">{t('Mobile network')} · ↓ ↑</div>
        <div className="grid grid-cols-2 gap-1">{Array.from({ length: 4 }, (_, i) => <div key={i} className="flex h-12 items-center justify-center rounded-lg border border-[#30343a] bg-[#202329] px-2 text-center text-sm font-semibold">{displayed[i] ? tileTitle(displayed[i]) : '—'}</div>)}</div>
        <div className="mt-2 rounded-lg border border-[#30343a] bg-[#202329] py-2 text-center text-sm font-semibold">▦ {t('More')}</div>
      </div>
    </section>
    <section aria-label={t('Display area')} className="min-w-0">
      <div className="mb-2 flex items-center justify-between text-[13px] font-semibold text-ink"><span>{t('Display area')}</span><span className="text-xs font-normal text-ink3">{items.length} / {CAPACITY}</span></div>
      <div data-menu-zone="display" className="max-w-[420px] rounded-2xl bg-[#0b0d11] p-3">
        <div aria-hidden="true" className="mb-3 flex items-center justify-between text-[#e8eaee]"><span className="font-bold">← {t('More')}</span><span className="text-xs text-[#9aa1ac]">▂▄▆ 5G ▰</span></div>
        <div className="grid grid-cols-3 gap-1">{Array.from({ length: CAPACITY }, (_, i) => cell(displayed[i], 'display', i))}</div>
      </div>
    </section>
    <section aria-label={t('Candidate area')} className="min-w-0">
      <div className="mb-2 flex items-center justify-between text-[13px] font-semibold text-ink"><span>{t('Candidate area')}</span><span className="text-xs font-normal text-ink3">{candidates.length}</span></div>
      <div data-menu-zone="candidate" className={`max-w-[420px] rounded-2xl bg-[#0b0d11] p-3 ${target?.zone === 'candidate' ? 'ring-2 ring-[#4c9eeb]' : ''}`}>
        <div className="grid grid-cols-3 gap-1">{Array.from({ length: Math.max(3, Math.ceil(candidates.length / 3) * 3) }, (_, i) => cell(candidates[i], 'candidate', i))}</div>
      </div>
    </section>
    {ghost && <div aria-hidden="true" className="pointer-events-none fixed z-50 w-28 rounded-lg border border-[#4c9eeb] bg-[#202329] px-2 py-2 text-sm font-semibold text-white shadow-lg" style={{ left: ghost.x + 12, top: ghost.y + 12 }}>{tileTitle(pages.find(p => p.id === ghost.id)!)}</div>}
  </div>
}
