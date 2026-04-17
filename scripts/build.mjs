import { execSync } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, readdirSync, rmSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const projectRoot = path.resolve(__dirname, '..');

const buildDir = path.join(projectRoot, 'build');
rmSync(buildDir, { recursive: true, force: true });
mkdirSync(buildDir, { recursive: true });

const tscCommand = 'npx tsc --project tsconfig.json';
execSync(tscCommand, { stdio: 'inherit', cwd: projectRoot });

const copyRecursive = (source, destination) => {
  if (!existsSync(source)) {
    return;
  }
  cpSync(source, destination, { recursive: true });
};

const pagesDir = path.join(projectRoot, 'src', 'pages');
if (!existsSync(pagesDir)) {
  console.warn('No HTML source directory found at', pagesDir);
} else {
  for (const entry of readdirSync(pagesDir, { withFileTypes: true })) {
    if (!entry.isFile() || !entry.name.endsWith('.html')) {
      continue;
    }
    const source = path.join(pagesDir, entry.name);
    const destination = path.join(buildDir, entry.name);
    cpSync(source, destination);
  }
}

copyRecursive(path.join(projectRoot, 'assets'), path.join(buildDir, 'assets'));
copyRecursive(path.join(projectRoot, 'support_files'), path.join(buildDir, 'support_files'));
copyRecursive(path.join(projectRoot, 'src', 'assets'), path.join(buildDir, 'assets'));

const manifestSource = path.join(projectRoot, 'src', 'manifest.json');
if (existsSync(manifestSource)) {
  cpSync(manifestSource, path.join(buildDir, 'manifest.json'));
}

const flasherSource = path.join(projectRoot, 'src', 'flasher.json');
if (existsSync(flasherSource)) {
  cpSync(flasherSource, path.join(buildDir, 'flasher.json'));
}

for (const binsDir of ['binsBeta', 'binsRelease']) {
  copyRecursive(path.join(projectRoot, 'src', binsDir), path.join(buildDir, binsDir));
}

