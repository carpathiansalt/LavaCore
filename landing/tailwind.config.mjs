/** @type {import('tailwindcss').Config} */
export default {
  content: ['./src/**/*.{astro,html,js,jsx,md,mdx,svelte,ts,tsx,vue}'],
  theme: {
    extend: {
      colors: {
        obsidian: '#0A0A0A',
        charcoal: '#1A1A1A',
        ember: '#FF4D00',
        flare: '#FF7A00',
        magma: '#FFC857',
        crimson: '#CC3300',
        ash: '#EADACB',
        paper: '#F5EDE5',
      },
      fontFamily: {
        display: ['"Bebas Neue"', 'ui-sans-serif', 'system-ui'],
        ui: ['Rajdhani', 'ui-sans-serif', 'system-ui'],
        body: ['Inter', 'ui-sans-serif', 'system-ui'],
      },
    },
  },
  plugins: [],
};
