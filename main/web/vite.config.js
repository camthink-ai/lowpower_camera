import { defineConfig } from 'vite'
import htmlMinifierTerser from 'vite-plugin-html-minifier-terser';
import removeConsole from "vite-plugin-remove-console";

// https://vitejs.dev/config/
export default defineConfig({
  base: "./",
  server: {
    // whether to enable https
    https: false,
    // port number
    port: 8080,
    host: "0.0.0.0",
    // local cross-origin proxy
    proxy: {
      "/api/v1": {
        target: "http://192.168.1.1",
        // target: "http://yapi.milesight.com/mock/228", // Mock Yapi
        changeOrigin: true,
        secure: false,
        pathRewrite: {
          "^/api/v1": "/api/v1"
        }
      }
    }
  },
  plugins: [
    // remove console logs in build
    removeConsole(),
    // compress HTML
    htmlMinifierTerser({
      removeAttributeQuotes: true, 
      collapseWhitespace: true,
      removeComments: true
    }),
  ],
  build: {
    assetsInlineLimit: 51200,
    target: 'esnext',
    // prevent vite from converting rgba() colors to #RGBA hex, compatible with mobile
    cssTarget: 'chrome61',
    minify: 'esbuild',
    sourcemap: false,
    rollupOptions: {
      output: {
        // fixed build output file naming
        entryFileNames: `assets/[name].js`,
        chunkFileNames: `assets/[name].js`,
        assetFileNames: `assets/[name].[ext]`,
      }
    }
  }
})
