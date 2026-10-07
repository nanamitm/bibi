import { defineConfig } from '@playwright/test';
export default defineConfig({testDir:'./tests',use:{baseURL:'http://127.0.0.1:4173/bibi/',headless:true},webServer:{command:'npm run build -- --base=/bibi/ && npm run preview -- --port 4173 --base=/bibi/',url:'http://127.0.0.1:4173/bibi/',reuseExistingServer:!process.env.CI}});

