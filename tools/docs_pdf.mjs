// Документация набора в PDF: по файлу на каждую часть и одна общая книга с оглавлением — на каждом языке.
//
//   node tools/docs_pdf.mjs [папка_для_PDF]        (по умолчанию build/docs-pdf)
//
// Разбор Markdown — библиотекой markdown-it, уже установленной в проекте сайта (~/Documents/cloude_work), отдельно
// ничего не ставится. Страницы для печати кладутся рядом (html/), в PDF их превращает Chrome без окна — со своим
// временным профилем, настройки и вкладки владельца не затрагиваются. Картинки (схемы, снимки экранов) берутся
// из docs/ по относительным ссылкам. Шрифт — system-ui (Roboto на этом Mac стоит одним жирным начертанием).
import { createRequire } from 'module';
import { readFileSync, writeFileSync, mkdirSync, readdirSync, existsSync, rmSync } from 'fs';
import { execFileSync } from 'child_process';
import path from 'path';
import { fileURLToPath, pathToFileURL } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const OUT = path.resolve(process.argv[2] || path.join(ROOT, 'build', 'docs-pdf'));
const SITE = path.join(process.env.HOME, 'Documents', 'cloude_work', 'node_modules', '.pnpm');
const mdDir = existsSync(SITE) ? readdirSync(SITE).filter((d) => d.startsWith('markdown-it@')).sort().pop() : null;
if (!mdDir) { console.error('нет markdown-it в ~/Documents/cloude_work/node_modules/.pnpm'); process.exit(1); }
const require = createRequire(path.join(SITE, mdDir, 'node_modules', 'markdown-it', 'package.json'));
const md = new (require('markdown-it'))({ html: true, linkify: true });

const CHROME = ['/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/Applications/Chromium.app/Contents/MacOS/Chromium']
  .find((p) => existsSync(p));
if (!CHROME) { console.error('нет браузера Chrome — PDF не сделать'); process.exit(1); }

// части по порядку чтения: имя файла → подпись
const PARTS = {
  uk: { book: 'Документація набору (українською)', toc: 'Зміст', parts: [
    ['user-guide.md', 'Користування'], ['hardware.md', 'Обладнання та схеми'], ['install.md', 'Встановлення'],
    ['commands.md', 'Команди порту'], ['technical.md', 'Технічний опис'], ['troubleshooting.md', 'Несправності'],
    ['development.md', 'Розробка'] ] },
  en: { book: 'Kit documentation (English)', toc: 'Contents', parts: [
    ['user-guide.md', 'Usage'], ['hardware.md', 'Hardware and schematics'], ['install.md', 'Installation'],
    ['commands.md', 'Serial commands'], ['technical.md', 'Technical description'], ['troubleshooting.md', 'Troubleshooting'],
    ['development.md', 'Development'] ] },
};

const CSS = `
@page { size: A4; margin: 16mm 14mm; }
body { font-family: system-ui, "Helvetica Neue", Arial, sans-serif; font-size: 10.5pt; line-height: 1.42; color: #111; }
h1 { font-size: 20pt; margin: 0 0 8pt; } h2 { font-size: 14pt; margin: 16pt 0 6pt; border-bottom: 1px solid #ccc; }
h3 { font-size: 12pt; margin: 12pt 0 4pt; } p, li { orphans: 3; widows: 3; }
table { border-collapse: collapse; margin: 6pt 0; width: 100%; font-size: 9.5pt; page-break-inside: auto; }
th, td { border: 1px solid #bbb; padding: 3pt 5pt; vertical-align: top; } th { background: #f0f2f4; }
tr { page-break-inside: avoid; } img { max-width: 100%; page-break-inside: avoid; }
code { font-family: Menlo, monospace; font-size: 9pt; background: #f3f3f3; padding: 0 2pt; }
pre { background: #f6f6f6; padding: 6pt; white-space: pre-wrap; font-size: 8.5pt; }
.part { page-break-before: always; } .cover h1 { font-size: 24pt; margin-top: 60mm; } .toc li { margin: 3pt 0; }
a { color: #1a55a8; text-decoration: none; }`;

function render(lang, file) {
  const src = path.join(ROOT, 'docs', lang, file);
  let html = md.render(readFileSync(src, 'utf8'));
  // картинки и ссылки на файлы проекта — абсолютными адресами (страница лежит в другой папке)
  html = html.replace(/(src|href)="(?!https?:|#|mailto:)([^"]+)"/g, (m, attr, ref) => {
    const [p, hash] = ref.split('#');
    if (attr === 'href' && p.endsWith('.md')) return `${attr}="#${hash || 'part-' + path.basename(p, '.md')}"`;   // в книге — переход к части
    const abs = path.resolve(path.dirname(src), decodeURI(p));
    return `${attr}="${pathToFileURL(abs).href}${hash ? '#' + hash : ''}"`;
  });
  return html;
}
const page = (title, body) => `<!doctype html><html><head><meta charset="utf-8"><title>${title}</title><style>${CSS}</style></head><body>${body}</body></html>`;

function pdf(htmlFile, pdfFile) {
  const profile = path.join(OUT, '.chrome-profile');
  execFileSync(CHROME, ['--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--generate-pdf-document-outline',
    `--user-data-dir=${profile}`, '--allow-file-access-from-files', `--print-to-pdf=${pdfFile}`, pathToFileURL(htmlFile).href], { stdio: 'ignore' });
  if (!existsSync(pdfFile)) throw new Error('PDF не получился: ' + pdfFile);
}

mkdirSync(path.join(OUT, 'html'), { recursive: true });
for (const [lang, def] of Object.entries(PARTS)) {
  mkdirSync(path.join(OUT, lang), { recursive: true });
  let book = `<section class="cover"><h1>${def.book}</h1><p>${new Date().toISOString().slice(0, 10)}</p><h2>${def.toc}</h2><ol class="toc">`;
  book += def.parts.map(([f, t]) => `<li><a href="#part-${path.basename(f, '.md')}">${t}</a></li>`).join('') + '</ol></section>';
  for (const [file, title] of def.parts) {
    const body = render(lang, file);
    const name = `${String(def.parts.findIndex((x) => x[0] === file) + 1).padStart(2, '0')} ${title}`;
    const h = path.join(OUT, 'html', `${lang}-${path.basename(file, '.md')}.html`);
    writeFileSync(h, page(title, body));
    pdf(h, path.join(OUT, lang, `${name}.pdf`));
    book += `<section class="part" id="part-${path.basename(file, '.md')}">${body}</section>`;
    console.log('готово:', lang, name);
  }
  const hb = path.join(OUT, 'html', `${lang}-book.html`);
  writeFileSync(hb, page(def.book, book));
  pdf(hb, path.join(OUT, `${def.book}.pdf`));
  console.log('готово:', def.book);
}
rmSync(path.join(OUT, '.chrome-profile'), { recursive: true, force: true });
