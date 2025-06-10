import { defineConfig } from 'vite'
import htmlMinifierTerser from 'vite-plugin-html-minifier-terser';
import removeConsole from "vite-plugin-remove-console";

// https://vitejs.dev/config/
export default defineConfig({
  base: "./",
  server: {
    // 是否开启 https
    https: false,
    // 端口号
    port: 8080,
    host: "0.0.0.0",
    // 本地跨域代理
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
    // 打包移除打印
    removeConsole(),
    // 压缩HTML
    htmlMinifierTerser({
      removeAttributeQuotes: true, 
      collapseWhitespace: true,
      removeComments: true
    }),
  ],
  build: {
    assetsInlineLimit: 51200,
    target: 'esnext',
    // 防止 vite 将 rgba() 颜色转化为 #RGBA 十六进制，兼容移动端
    cssTarget: 'chrome61',
    minify: 'esbuild',
    sourcemap: false,
    rollupOptions: {
      output: {
        // 固定打包output文件命名
        entryFileNames: `assets/[name].js`,
        chunkFileNames: `assets/[name].js`,
        assetFileNames: `assets/[name].[ext]`,
      }
    }
  }
})
