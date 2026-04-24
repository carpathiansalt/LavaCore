# LavaCore Landing

Astro 5 + Tailwind landing site for LavaCore, structured in the same spirit as
AstroWind: static output, strong SEO defaults, reusable layout, and focused
marketing/docs pages.

## Commands

```sh
npm install
npm run dev
npm run check
npm run build
```

## Firebase Hosting

The Flutter web app remains on the existing default hosting config. This site
deploys to the separate `landing` target in the same `led-runner` project.

```sh
firebase hosting:sites:create led-runner-landing --project led-runner
firebase target:apply hosting landing led-runner-landing --project led-runner
npm run build
cd ../new_app2
firebase deploy --only hosting:landing --project led-runner
```
