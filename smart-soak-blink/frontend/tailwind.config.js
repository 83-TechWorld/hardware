/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        background: "#0b1326",
        "on-surface": "#dae2fd",
        "on-surface-variant": "#cbc3d7",
        "surface-container": "#171f33",
        "surface-container-high": "#222a3d",
        "surface-container-lowest": "#060e20",
        primary: "#d0bcff",
        "primary-fixed-dim": "#d0bcff",
        secondary: "#4edea3",
        "secondary-container": "#00a572",
        outline: "#958ea0",
        "outline-variant": "#494454",
        error: "#ffb4ab",
      },
    },
  },
  plugins: [],
}
