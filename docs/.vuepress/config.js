const { defaultTheme } = require('vuepress')
const { searchPlugin } = require('@vuepress/plugin-search')

module.exports = {
  lang: 'en-US',
  title: 'hydrasdr_433',
  description: 'HydraSDR wideband data receiver for ISM/SRD bands.',

  base: '/hydrasdr_433/',
  markdown: {
    code: {
      lineNumbers: false,
    },
  },

  plugins: [
    searchPlugin(),
  ],

  theme: defaultTheme({
    repo: 'hydrasdr/hydrasdr_433',
    displayAllHeaders: true,

    editLink: true,
    docsBranch: 'master',
    docsDir: 'docs',

    navbar: [
      { text: 'Projects', link: 'https://triq.org/' },
    ],

    sidebar: [
      { text: 'Overview', link: '/' },
      'BUILDING',
      'BINARY_BUILDS',
      'STARTING',
      'CHANGELOG',
      'CONTRIBUTING',
      'PRIMER',
      'IQ_FORMATS',
      'ANALYZE',
      'OPERATION',
      'DATA_FORMAT',
      'HARDWARE',
      'INTEGRATION',
      'LINKS',
      'TESTS',
    ],
  }),
};
