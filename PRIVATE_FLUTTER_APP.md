# Flutter app (private)

The **LavaCore Flutter application** lives in `new_app2/` in a full developer workspace. That directory is listed in the **root `.gitignore`** so it is **not committed** to the public `LavaCore` GitHub repository.

## Keeping the app in a private Git repo

From your machine (with `new_app2/` present):

```bash
cd new_app2
git init
git add .
git commit -m "Initial private app import"
git branch -M main
git remote add origin https://github.com/carpathiansalt/LavaCore-app.git
git push -u origin main
```

Use any **private** repository name you prefer. Do not push `google-services.json` or other secrets if they are not already ignored — check `new_app2/.gitignore`.

## Full monorepo (optional)

If you want one **private** GitHub repo that contains both `firmware/` and `new_app2/`, remove the `/new_app2/` line from the root `.gitignore`, then `git add` and push to a **private** remote instead of the public firmware-only layout.
