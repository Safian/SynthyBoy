# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('knowledge', Path(__file__).parents[1]/'mpc_expert_knowledge.py')
k = importlib.util.module_from_spec(spec)
spec.loader.exec_module(k)

class KnowledgeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)/'knowledge'
        self.root.mkdir()
        self.project = Path(self.temp.name)/'repo'
        self.source = self.root/'manual.txt'
        self.source.write_text('=====PAGE 387\nNumeric keypad enters values.\n')

    def test_incremental_page_citations_deletion_and_private(self):
        private = self.root/'private-do-not-publish'
        private.mkdir()
        (private/'secret.md').write_text('Numeric private leak')
        k.index(self.root,self.project)
        found = k.search(self.root,'Numeric')
        self.assertEqual(len(found),1)
        self.assertEqual(found[0]['citation'],'page 387')
        self.assertEqual(k.index(self.root,self.project)['changed'],0)
        self.source.write_text('=====PAGE 40\nEnter navigates menus')
        k.index(self.root,self.project)
        self.assertEqual(k.search(self.root,'Numeric'),[])
        self.assertEqual(k.search(self.root,'navigates')[0]['citation'],'page 40')
        self.source.unlink()
        k.index(self.root,self.project)
        self.assertEqual(k.search(self.root,'navigates'),[])

    def test_failed_download_retains_source_and_usable_index(self):
        k.index(self.root,self.project)
        registry = self.root/'registry.json'
        registry.write_text(json.dumps({'sources':[{'id':'bad','kind':'text','path':'manual.txt','url':'https://example.invalid'}]}))
        with patch.object(k,'fetch',side_effect=OSError('offline')):
            result = k.refresh(self.root,self.project,registry)
        self.assertEqual(result['sources'][0]['status'],'failed')
        self.assertTrue(k.search(self.root,'Numeric'))

    def test_lock_prevents_concurrent_refresh(self):
        registry=self.root/'registry.json'
        registry.write_text('{"sources":[]}')
        with k.lock(self.root):
            self.assertEqual(k.refresh(self.root,self.project,registry),{'status':'already-running'})

    def test_failed_index_leaves_old_snapshot_readable(self):
        k.index(self.root,self.project)
        self.source.write_text('Replacement contents')
        with patch.object(k,'chunks',side_effect=RuntimeError('interrupted')):
            with self.assertRaises(RuntimeError): k.index(self.root,self.project)
        self.assertTrue(k.search(self.root,'Numeric'))

    def test_discovered_pdf_is_downloaded_extracted_and_searchable(self):
        registry=self.root/'registry.json'
        registry.write_text(json.dumps({'sources':[{'id':'official','kind':'discovery','path':'web-pages/discovery.html','url':'https://www.akaipro.com/downloads/'}]}))
        url='https://cdn.inmusicbrands.com/MPC%20XL%20-%20User%20Guide%20-%20v3.11.0.pdf'
        def remote(address, maximum, cached=None):
            return ((f'<a href="{url}">Manual</a>'.encode() if 'akaipro' in address else b'PDF'), {'url':address})
        with patch.object(k,'fetch',side_effect=remote), patch.object(k,'pdf_text',return_value=b'=====PAGE 1\nManual Version v3.11.0\nNovelkeypad navigation'):
            report=k.refresh(self.root,self.project,registry)
        discovery=report['sources'][0]['discovered_sources'][0]
        self.assertEqual(discovery['status'],'ok')
        self.assertEqual(discovery['document_version'],'3.11.0')
        self.assertEqual(k.search(self.root,'Novelkeypad')[0]['citation'],'page 1')
        with patch.object(k,'fetch',side_effect=OSError('offline')):
            k.refresh(self.root,self.project,registry)
        self.assertTrue(k.search(self.root,'Novelkeypad'))

    def test_discovery_reuses_registered_manual_without_duplicate(self):
        url='https://cdn.inmusicbrands.com/MPC%20XL%20User%20Guide.pdf'
        known={'id':'known-xl','kind':'pdf','path':'manuals-extended/canonical.pdf','url':url}
        page={'id':'official','kind':'discovery','path':'web-pages/discovery.html','url':'https://www.akaipro.com/downloads/'}
        registry=self.root/'registry.json'
        registry.write_text(json.dumps({'sources':[known,page]}))
        def remote(address, maximum, cached=None):
            return ((f'<a href="{url}">Manual</a>'.encode() if 'akaipro' in address else b'PDF'), {'url':address})
        with patch.object(k,'fetch',side_effect=remote) as fetched, patch.object(k,'pdf_text',return_value=b'=====PAGE 1\nCanonical keypad'):
            report=k.refresh(self.root,self.project,registry)
        self.assertEqual(sum(call.args[0]==url for call in fetched.call_args_list),1)
        self.assertTrue((self.root/known['path']).exists())
        self.assertFalse((self.root/'sources/runtime-discovered-manuals').exists())
        self.assertEqual(report['sources'][1]['discovered_sources'][0]['status'],'registered-source')

    def test_conditional_response_missing_pdf_text_refetches(self):
        target=self.root/'fresh.pdf'
        target.write_bytes(b'PDF')
        source={'id':'pdf','kind':'pdf','path':'fresh.pdf','url':'https://cdn.inmusicbrands.com/manual.pdf'}
        with patch.object(k,'fetch',side_effect=[(None,{}),(b'PDF',{})]) as fetch, patch.object(k,'pdf_text',return_value=b'=====PAGE 1\nRestored'):
            result=k.refresh_source(self.root,source)
        self.assertEqual(result['status'],'ok')
        self.assertEqual(fetch.call_count,2)
        self.assertIn('Restored',target.with_suffix('.txt').read_text())

    def test_release_cover_beats_historical_versions_and_repairs_cached_metadata(self):
        text='=====PAGE 1\nMPC 3.10.1 - Release Notes\nMPC 3.10.1 Update\nStandalone Mode on version 3.4 to 3.10.1\n=====PAGE 2\nVersion 2.15'
        self.assertEqual(k.document_version(text),'3.10.1')
        source={'id':'release','kind':'pdf','path':'release.pdf','url':'https://cdn.inmusicbrands.com/release.pdf'}
        (self.root/'release.pdf').write_bytes(b'PDF')
        (self.root/'release.txt').write_text(text)
        cache=self.root/'.runtime/source-release.json'
        k.atomic(cache,json.dumps({'id':'release','document_version':'3.4'}).encode())
        with patch.object(k,'fetch',return_value=(None,{})):
            result=k.refresh_source(self.root,source)
        self.assertEqual(result['document_version'],'3.10.1')
        self.assertEqual(json.loads(cache.read_text())['document_version'],'3.10.1')

    def test_manual_cover_version_does_not_use_compatibility_prose(self):
        self.assertEqual(k.document_version('=====PAGE 1\nUser Guide\nManual Version v3.10.1\n=====PAGE 2\nVersion 3.4'),'3.10.1')
        self.assertIsNone(k.document_version('=====PAGE 1\nCompatible with version 3.4'))

    def test_pdf_page_extraction(self):
        class Page:
            def extract_text(self): return 'Keypad'
        with patch('pypdf.PdfReader',return_value=type('Reader',(),{'pages':[Page(),Page()]})()):
            self.assertEqual(k.pdf_text(b'fake'),'=====PAGE 1\nKeypad\n=====PAGE 2\nKeypad'.encode())

if __name__=='__main__': unittest.main()
